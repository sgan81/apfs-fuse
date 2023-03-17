/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstring>

#include <vector>
#include <iostream>

#include "Global.h"
#include "Endian.h"
#include <Crypto/Crypto.h>
#include <Crypto/TripleDes.h>
#include "Util.h"

#include "DiskImageFile.h"

#pragma pack(push, 1)

struct DmgCryptHeaderV1
{
	uint8_t uuid[0x10];
	be_uint32_t block_size;
	be_uint32_t unk_14;
	be_uint32_t unk_18;
	be_uint32_t unk_1C;
	be_uint32_t unk_20;
	be_uint32_t unk_24;
	be_uint32_t kdf_algorithm;
	be_uint32_t kdf_prng_algorithm;
	be_uint32_t kdf_iteration_count;
	be_uint32_t kdf_salt_len;
	uint8_t kdf_salt[0x20];
	be_uint32_t unk_58;
	be_uint32_t unk_5C;
	be_uint32_t unk_60;
	be_uint32_t unk_64;
	uint8_t unwrap_iv[0x20];
	be_uint32_t wrapped_aes_key_len;
	uint8_t wrapped_aes_key[0x100];
	be_uint32_t unk_18C;
	be_uint32_t unk_190;
	uint8_t unk_194[0x20];
	be_uint32_t wrapped_hmac_sha1_key_len;
	uint8_t wrapped_hmac_sha1_key[0x100];
	be_uint32_t unk_2B8;
	be_uint32_t unk_2BC;
	uint8_t unk_2C0[0x20];
	be_uint32_t wrapped_integrity_key_len;
	uint8_t wrapped_integrity_key[0x100];
	be_uint32_t unk_3E8_len;
	uint8_t unk_3E8[0x100];
	be_uint64_t decrypted_data_length;
	be_uint32_t unk_4F0;
	char signature[8];
};

static_assert(sizeof(DmgCryptHeaderV1) == 0x4FC, "DmgCryptHeaderV1 invalid length.");

struct DmgCryptHeaderV2
{
	char signature[8];
	be_uint32_t maybe_version;
	be_uint32_t unk_0C;
	be_uint32_t unk_10;
	be_uint32_t unk_14;
	be_uint32_t key_bits;
	be_uint32_t unk_1C;
	be_uint32_t unk_20;
	uint8_t uuid[0x10];
	be_uint32_t block_size;
	be_uint64_t encrypted_data_length;
	be_uint64_t encrypted_data_offset;
	be_uint32_t no_of_keys;
};

struct DmgKeyPointer
{
	be_uint32_t key_type;
	be_uint64_t key_offset;
	be_uint64_t key_length;
};

struct DmgKeyData
{
	be_uint32_t kdf_algorithm;
	be_uint32_t prng_algorithm;
	be_uint32_t iteration_count;
	be_uint32_t salt_len;
	uint8_t salt[0x20];
	be_uint32_t blob_enc_iv_size;
	uint8_t blob_enc_iv[0x20];
	be_uint32_t blob_enc_key_bits;
	be_uint32_t blob_enc_algorithm;
	be_uint32_t blob_unk_5C;
	be_uint32_t blob_enc_mode;
	be_uint32_t encr_key_blob_size;
	uint8_t encr_key_blob[0x200];
};

#pragma pack(pop)

DiskImageFile::DiskImageFile()
{
	m_is_encrypted = false;

	m_crypt_offset = 0;
	m_crypt_size = 0;
	m_crypt_blocksize = 0;
}

DiskImageFile::~DiskImageFile()
{
}

bool DiskImageFile::Open(const char * name)
{
	m_image.open(name, std::ios::binary);

	return m_image.is_open();
}

void DiskImageFile::Close()
{
	m_image.close();

	m_crypt_blocksize = 0;
	m_crypt_size = 0;
	m_crypt_offset = 0;
}

void DiskImageFile::Reset()
{
	m_is_encrypted = false;
	m_crypt_offset = 0;
	m_crypt_size = 0;
	m_crypt_blocksize = 0;
	memset(m_hmac_key, 0, sizeof(m_hmac_key));
	m_aes.CleanUp();
}

bool DiskImageFile::CheckSetupEncryption()
{
	char signature[8];

	m_image.seekg(0, std::ios::end);

	m_is_encrypted = false;
	m_crypt_offset = 0;
	m_crypt_size = m_image.tellg();

	m_image.seekg(-8, std::ios::end);
	m_image.read(signature, 8);

	if (!memcmp(signature, "cdsaencr", 8))
	{
		m_is_encrypted = true;

		if (!SetupEncryptionV1())
		{
			m_image.close();
			fprintf(stderr, "Error setting up decryption V1.\n");
			return false;
		}
	}

	m_image.seekg(0);
	m_image.read(signature, 8);

	if (!memcmp(signature, "encrcdsa", 8))
	{
		m_is_encrypted = true;

		if (!SetupEncryptionV2())
		{
			m_image.close();
			fprintf(stderr, "Error setting up decryption V2.\n");
			return false;
		}
	}

	return true;
}

void DiskImageFile::Read(uint64_t off, void * data, size_t size)
{
	if (!m_is_encrypted)
	{
		m_image.seekg(off);
		m_image.read(reinterpret_cast<char *>(data), size);
	}
	else
	{
		uint8_t buffer[0x1000];
		uint64_t mask = m_crypt_blocksize - 1;
		uint32_t blkid;
		uint8_t iv[0x14];
		size_t rd_len;
		uint8_t *bdata = reinterpret_cast<uint8_t *>(data);

		if (off & mask)
		{
			blkid = static_cast<uint32_t>(off / m_crypt_blocksize);
			blkid = bswap_be(blkid);

			m_image.seekg(m_crypt_offset + (off & ~mask));
			m_image.read(reinterpret_cast<char *>(buffer), m_crypt_blocksize);

			HMAC_SHA1(m_hmac_key, 0x14, reinterpret_cast<const uint8_t *>(&blkid), sizeof(uint32_t), iv);

			m_aes.SetIV(iv);
			m_aes.DecryptCBC(buffer, buffer, m_crypt_blocksize);

			rd_len = m_crypt_blocksize - (off & mask);
			if (rd_len > size)
				rd_len = size;

			memcpy(bdata, buffer + (off & mask), rd_len);

			bdata += rd_len;
			off += rd_len;
			size -= rd_len;
		}

		while (size > 0)
		{
			blkid = static_cast<uint32_t>(off / m_crypt_blocksize);
			blkid = bswap_be(blkid);

			m_image.seekg(m_crypt_offset + (off & ~mask));
			m_image.read(reinterpret_cast<char *>(buffer), m_crypt_blocksize);

			HMAC_SHA1(m_hmac_key, 0x14, reinterpret_cast<const uint8_t *>(&blkid), sizeof(uint32_t), iv);

			m_aes.SetIV(iv);
			m_aes.DecryptCBC(buffer, buffer, m_crypt_blocksize);

			rd_len = m_crypt_blocksize;
			if (rd_len > size)
				rd_len = size;

			memcpy(bdata, buffer, rd_len);

			bdata += rd_len;
			off += rd_len;
			size -= rd_len;
		}
	}
}

bool DiskImageFile::SetupEncryptionV1()
{
	DmgCryptHeaderV1 hdr;
	TripleDES des;
	std::string password;
	uint8_t derived_key[0x18];
	size_t n;
	uint64_t total_size;

	static const uint8_t des_iv[8] = { 0x4A, 0xDD, 0xA2, 0x2C, 0x79, 0xE8, 0x21, 0x05 };
	// uint8_t aes_key[0x10];
	// uint8_t hmac_key[0x14];
	// uint8_t integrity_key[0x100];
	uint8_t tmp_1[0x100];
	uint8_t tmp_2[0x100];
	uint8_t tmp_3[0x100];
	size_t len;

	int64_t hdrsize = sizeof(DmgCryptHeaderV1);

	m_image.seekg(-hdrsize, std::ios::end);
	total_size = m_image.tellg();
	m_image.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));

	if (g_debug & Dbg_Crypto)
	{
		std::cout.flags(std::cout.hex | std::cout.uppercase);
		std::cout.fill('0');
	}

	m_crypt_size = hdr.decrypted_data_length;
	m_crypt_blocksize = hdr.block_size;
	m_crypt_offset = 0;
	m_is_encrypted = true;

	std::cout << "Password: ";
	GetPassword(password);

	PBKDF2_HMAC_SHA1(reinterpret_cast<const uint8_t *>(password.c_str()), password.size(), hdr.kdf_salt, hdr.kdf_salt_len, hdr.kdf_iteration_count, derived_key, 0x18);

	if (g_debug & Dbg_Crypto)
	{
		std::cout << "Salt: " << hexstr(hdr.kdf_salt, hdr.kdf_salt_len) << std::endl;
		std::cout << "Iter: " << hdr.kdf_iteration_count << std::endl;
		std::cout << "DKey: " << hexstr(derived_key, sizeof(derived_key)) << std::endl;

		std::cout << "IV  : " << hexstr(hdr.unwrap_iv, sizeof(hdr.unwrap_iv)) << std::endl;
		std::cout << "U3E8: " << hexstr(hdr.unk_3E8, hdr.unk_3E8_len) << std::endl;
		std::cout << std::endl;

		std::cout << "AES Key:" << std::endl;
	}

	des.SetKey(derived_key);

	len = hdr.wrapped_aes_key_len;

	des.SetIV(des_iv);
	des.DecryptCBC(tmp_1, hdr.wrapped_aes_key, len);

	if (g_debug & Dbg_Crypto)
		DumpHex(std::cout, tmp_1, len);

	if (tmp_1[len - 1] > 0x8)
		return false;

	len = PkcsUnpad(tmp_1, len);

	for (n = 0; n < len; n++)
		tmp_2[len - n - 1] = tmp_1[n];

	if (g_debug & Dbg_Crypto)
		DumpHex(std::cout, tmp_2, len);

	des.SetIV(nullptr);

	des.DecryptCBC(tmp_3, tmp_2, len);

	if (g_debug & Dbg_Crypto)
	{
		DumpHex(std::cout, tmp_3, len);
		std::cout << std::endl;
	}

	if (tmp_3[len - 1] > 0x8)
		return false;

	len = PkcsUnpad(tmp_3, len);

	// memcpy(aes_key, tmp_3 + 12, 0x10);
	m_aes.SetKey(tmp_3 + 12, AES::AES_128);

	if (g_debug & Dbg_Crypto)
		std::cout << "HMAC SHA1 Key:" << std::endl;

	len = hdr.wrapped_hmac_sha1_key_len;

	des.SetIV(des_iv);
	des.DecryptCBC(tmp_1, hdr.wrapped_hmac_sha1_key, len);

	if (g_debug & Dbg_Crypto)
		DumpHex(std::cout, tmp_1, len);

	if (tmp_1[len - 1] > 0x8)
		return false;

	len = PkcsUnpad(tmp_1, len);

	for (n = 0; n < len; n++)
		tmp_2[len - n - 1] = tmp_1[n];

	if (g_debug & Dbg_Crypto)
		DumpHex(std::cout, tmp_2, len);

	des.SetIV(nullptr);

	des.DecryptCBC(tmp_3, tmp_2, len);

	if (g_debug & Dbg_Crypto)
	{
		DumpHex(std::cout, tmp_3, len);
		std::cout << std::endl;
	}

	if (tmp_3[len - 1] > 0x8)
		return false;

	len = PkcsUnpad(tmp_3, len);

	// memcpy(hmac_key, tmp_3 + 12, 0x14);
	memcpy(m_hmac_key, tmp_3 + 12, 0x14);

	if (g_debug & Dbg_Crypto)
		std::cout << "Integrity Key:" << std::endl;

	len = hdr.wrapped_integrity_key_len;
	des.SetIV(des_iv);
	des.DecryptCBC(tmp_1, hdr.wrapped_integrity_key, len);

	if (g_debug & Dbg_Crypto)
		DumpHex(std::cout, tmp_1, len);

	if (tmp_1[len - 1] > 0x8)
		return false;

	len = PkcsUnpad(tmp_1, len);

	for (n = 0; n < len; n++)
		tmp_2[len - n - 1] = tmp_1[n];

	if (g_debug & Dbg_Crypto)
		DumpHex(std::cout, tmp_2, len);

	des.SetIV(tmp_2);
	len -= 8;

	des.DecryptCBC(tmp_3, tmp_2 + 8, len);

	if (g_debug & Dbg_Crypto)
	{
		DumpHex(std::cout, tmp_3, len);
		std::cout << std::endl;

		std::cout << "Unknown Key:" << std::endl;
		DumpHex(std::cout, hdr.unk_3E8, hdr.unk_3E8_len);
	}

	return true;
}

bool DiskImageFile::SetupEncryptionV2()
{
	std::vector<uint8_t> data;
	std::vector<uint8_t> kdata;

	const DmgCryptHeaderV2 *hdr;
	const DmgKeyPointer *keyptr;
	const DmgKeyData *keydata;
	std::string password;
	uint32_t no_of_keys;
	uint32_t key_id;

	uint8_t derived_key[0x18];
	uint8_t blob[0x200];
	uint32_t blob_len;

	TripleDES des;

	bool key_ok = false;

	data.resize(0x1000);

	m_image.seekg(0);
	m_image.read(reinterpret_cast<char *>(data.data()), data.size());

	hdr = reinterpret_cast<const DmgCryptHeaderV2 *>(data.data());

	if (memcmp(hdr->signature, "encrcdsa", 8))
		return false;

	m_crypt_offset = hdr->encrypted_data_offset;
	m_crypt_size = hdr->encrypted_data_length;
	m_crypt_blocksize = hdr->block_size;

	std::cout << "Encryped DMG detected." << std::endl;
	std::cout << "Password: ";
	GetPassword(password);

	no_of_keys = hdr->no_of_keys;

	for (key_id = 0; key_id < no_of_keys; key_id++)
	{
		keyptr = reinterpret_cast<const DmgKeyPointer *>(data.data() + sizeof(DmgCryptHeaderV2) + key_id * sizeof(DmgKeyPointer));

		kdata.resize(keyptr->key_length);
		m_image.seekg(keyptr->key_offset.get());
		m_image.read(reinterpret_cast<char *>(kdata.data()), kdata.size());

		keydata = reinterpret_cast<const DmgKeyData *>(kdata.data());

		PBKDF2_HMAC_SHA1(reinterpret_cast<const uint8_t *>(password.c_str()), password.size(), keydata->salt, keydata->salt_len, keydata->iteration_count, derived_key, sizeof(derived_key));

		des.SetKey(derived_key);
		des.SetIV(keydata->blob_enc_iv);

		blob_len = keydata->encr_key_blob_size;

		des.DecryptCBC(blob, keydata->encr_key_blob, blob_len);

		if (blob[blob_len - 1] < 1 || blob[blob_len - 1] > 8)
			continue;
		blob_len -= blob[blob_len - 1];

		if (g_debug & Dbg_Crypto)
		{
			std::cout << "Salt: " << hexstr(keydata->salt, keydata->salt_len) << std::endl;
			std::cout << "Iter: " << keydata->iteration_count << std::endl;
			std::cout << "DKey: " << hexstr(derived_key, sizeof(derived_key)) << std::endl;
			std::cout << "Blob: " << hexstr(keydata->encr_key_blob, keydata->encr_key_blob_size) << std::endl;
			std::cout << "DBlb: " << hexstr(blob, keydata->encr_key_blob_size) << std::endl;
			std::cout << "Key : " << hexstr(blob, hdr->key_bits / 8) << std::endl;
			std::cout << "HMAC: " << hexstr(blob + hdr->key_bits / 8, 0x14) << std::endl;
		}

		if (memcmp(blob + blob_len - 5, "CKIE", 4))
			continue;

		if (hdr->key_bits == 128)
		{
			m_aes.SetKey(blob, AES::AES_128);
			memcpy(m_hmac_key, blob + 0x10, 0x14);
			key_ok = true;
			break;
		}
		else if (hdr->key_bits == 256)
		{
			m_aes.SetKey(blob, AES::AES_256);
			memcpy(m_hmac_key, blob + 0x20, 0x14);
			key_ok = true;
			break;
		}
	}

	return key_ok;
}

size_t DiskImageFile::PkcsUnpad(const uint8_t *data, size_t size)
{
	if (data[size - 1] >= 1 && data[size - 1] <= 8)
		size -= data[size - 1];
	return size;
}

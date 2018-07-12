/*
This file is part of apfs-fuse, a read-only implementation of APFS
(Apple File System) for FUSE.
Copyright (C) 2017 Simon Gander

Apfs-fuse is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

Apfs-fuse is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstring>
#include <vector>
#include <iostream>
#include <iomanip>

#include "DeviceDMG.h"
#include "Util.h"
#include "PList.h"
#include "TripleDes.h"
#include "Crypto.h"
#include "DeviceImageDisk.h"

#pragma pack(push, 1)

struct DmgCryptHeaderV2
{
	char signature[8];
	be<uint32_t> maybe_version;
	be<uint32_t> unk_0C;
	be<uint32_t> unk_10;
	be<uint32_t> unk_14;
	be<uint32_t> key_bits;
	be<uint32_t> unk_1C;
	be<uint32_t> unk_20;
	uint8_t uuid[0x10];
	be<uint32_t> block_size;
	be<uint64_t> encrypted_data_length;
	be<uint64_t> encrypted_data_offset;
	be<uint32_t> no_of_keys;
};

struct DmgKeyPointer
{
	be<uint32_t> key_type;
	be<uint64_t> key_offset;
	be<uint64_t> key_length;
};

struct DmgKeyData
{
	be<uint32_t> kdf_algorithm;
	be<uint32_t> prng_algorithm;
	be<uint32_t> iteration_count;
	be<uint32_t> salt_len;
	uint8_t salt[0x20];
	be<uint32_t> blob_enc_iv_size;
	uint8_t blob_enc_iv[0x20];
	be<uint32_t> blob_enc_key_bits;
	be<uint32_t> blob_enc_algorithm;
	be<uint32_t> blob_unk_5C;
	be<uint32_t> blob_enc_mode;
	be<uint32_t> encr_key_blob_size;
	uint8_t encr_key_blob[0x200];
};

#pragma pack(pop)



DeviceImageDisk::DeviceImageDisk()
{
	m_is_encrypted = false;
	m_crypt_offset = 0;
	m_crypt_size = 0;
	m_crypt_blocksize = 0;
}

DeviceImageDisk::~DeviceImageDisk()
{
}

void DeviceImageDisk::ReadInternal(uint64_t off, void * data, size_t size)
{
	if (!m_is_encrypted)
	{
		ReadRaw(data, size, off);
//		m_dmg.seekg(off);
//		m_dmg.read(reinterpret_cast<char *>(data), size);
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

			ReadRaw(buffer, m_crypt_blocksize, m_crypt_offset + (off & ~mask));
//			m_dmg.seekg(m_crypt_offset + (off & ~mask));
//			m_dmg.read(reinterpret_cast<char *>(buffer), m_crypt_blocksize);

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

			ReadRaw(buffer, m_crypt_blocksize, m_crypt_offset + (off & ~mask));
//			m_dmg.seekg(m_crypt_offset + (off & ~mask));
//			m_dmg.read(reinterpret_cast<char *>(buffer), m_crypt_blocksize);

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

bool DeviceImageDisk::SetupEncryptionV2(std::ifstream& m_dmg)
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

	m_dmg.seekg(0);
	m_dmg.read(reinterpret_cast<char *>(data.data()), data.size());

	hdr = reinterpret_cast<const DmgCryptHeaderV2 *>(data.data());

	if (memcmp(hdr->signature, "encrcdsa", 8))
		return false;

	m_crypt_offset = hdr->encrypted_data_offset;
	m_crypt_size = hdr->encrypted_data_length;
	m_crypt_blocksize = hdr->block_size;

	#ifdef DEBUG
		password = "foo"; // Jief : I don't enjoy typing password over and over again
	#else
		std::cout << "Encryped DMG detected." << std::endl;
		std::cout << "Password: ";
		GetPassword(password);
	#endif

	no_of_keys = hdr->no_of_keys;

	for (key_id = 0; key_id < no_of_keys; key_id++)
	{
		keyptr = reinterpret_cast<const DmgKeyPointer *>(data.data() + sizeof(DmgCryptHeaderV2) + key_id * sizeof(DmgKeyPointer));

		kdata.resize(keyptr->key_length);
		m_dmg.seekg(keyptr->key_offset.get());
		m_dmg.read(reinterpret_cast<char *>(kdata.data()), kdata.size());

		keydata = reinterpret_cast<const DmgKeyData *>(kdata.data());

		PBKDF2_HMAC_SHA1(reinterpret_cast<const uint8_t *>(password.c_str()), password.size(), keydata->salt, keydata->salt_len, keydata->iteration_count, derived_key, sizeof(derived_key));

		des.SetKey(derived_key);
		des.SetIV(keydata->blob_enc_iv);

		blob_len = keydata->encr_key_blob_size;

		des.DecryptCBC(blob, keydata->encr_key_blob, blob_len);

		if (blob[blob_len - 1] < 1 || blob[blob_len - 1] > 8)
			continue;
		blob_len -= blob[blob_len - 1];

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

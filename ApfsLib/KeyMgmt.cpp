#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <assert.h>
#include <limits.h>

#include "ApfsContainer.h"
#include "AesXts.h"
#include "Sha256.h"
#include "Crypto.h"
#include "Util.h"
#include "BlockDumper.h"

#include "KeyMgmt.h"

struct bagdata_t
{
	const uint8_t *data;
	size_t size;
};

struct key_hdr_t
{
	apfs_uuid_t uuid;
	uint16_t type;
	uint16_t length;
	uint32_t unknown;
};

struct key_data_t
{
	const key_hdr_t *header;
	bagdata_t data;
};

struct keybag_hdr_t
{
	uint16_t version;
	uint16_t no_of_keys;
	uint32_t no_of_bytes;
};

struct key_extent_t
{
	uint64_t blk;
	uint64_t bcnt;
};

struct blob_header_t
{
	uint64_t unk_80;
	uint8_t hmac[0x20];
	uint8_t salt[0x08];
	bagdata_t blob;
};

struct key_unk_82_t
{
	uint32_t unk_00;
	uint16_t unk_04;
	uint8_t unk_06;
	uint8_t unk_07;
};

struct kek_blob_t
{
	uint64_t unk_80;
	apfs_uuid_t uuid;
	key_unk_82_t unk_82;
	uint8_t wrapped_kek[0x28];
	uint64_t iterations;
	uint8_t salt[0x10];
};

struct vek_blob_t
{
	uint64_t unk_80;
	apfs_uuid_t uuid;
	key_unk_82_t unk_82;
	uint8_t wrapped_vek[0x28];
};

KeyParser::KeyParser()
{
	m_start = nullptr;
	m_ptr = nullptr;
	m_end = nullptr;
}

void KeyParser::SetData(const uint8_t *data, size_t size)
{
	m_start = data;
	m_end = data + size;
	m_ptr = m_start;
}

void KeyParser::SetData(const bagdata_t& data)
{
	m_start = data.data;
	m_end = data.data + data.size;
	m_ptr = m_start;
}

void KeyParser::Rewind()
{
	m_ptr = m_start;
}

void KeyParser::Clear()
{
	m_start = nullptr;
	m_ptr = nullptr;
	m_end = nullptr;
}

bool KeyParser::GetUInt64(uint8_t expected_tag, uint64_t & result)
{
	uint8_t tag;
	size_t len;
	size_t k;
	uint64_t val;

	tag = *m_ptr;

	if (tag != expected_tag)
		return false;

	GetTagAndLen(tag, len);

	val = 0;
	for (k = 0; k < len; k++)
		val = (val << 8) | GetByte();

	result = val;

	return true;
}

bool KeyParser::GetBytes(uint8_t expected_tag, uint8_t * data, size_t len)
{
	uint8_t tag;

	tag = *m_ptr;

	if (tag != expected_tag)
		return false;

	GetTagAndLen(tag, len);

	if ((m_end - m_ptr) < static_cast<ptrdiff_t>(len))
		return false;

	memcpy(data, m_ptr, len);
	m_ptr += len;

	return true;
}

bool KeyParser::GetAny(uint8_t expected_tag, bagdata_t& data)
{
	uint8_t tag;
	size_t len;

	tag = *m_ptr;

	if (tag != expected_tag)
		return false;

	GetTagAndLen(tag, len);

	if ((m_end - m_ptr) < static_cast<ptrdiff_t>(len))
		return false;

	data.data = m_ptr;
	data.size = len;

	m_ptr += len;

	return true;
}

bool KeyParser::GetTagAndLen(uint8_t & tag, size_t & len)
{
	uint8_t b;

	tag = GetByte();
	b = GetByte();

	if (b >= 0x80)
	{
		len = 0;
		for (uint8_t k = 0; k < (b & 0x7F); k++)
		{
			len = (len << 8) | GetByte();
		}
	}
	else
		len = b;

	return true;
}

void KeyParser::GetRemaining(bagdata_t &data)
{
	data.data = m_ptr;
	data.size = m_end - m_ptr;
}

Keybag::Keybag()
{
}

Keybag::~Keybag()
{
}

bool Keybag::Init(const uint8_t * data, size_t size)
{
	const keybag_hdr_t &hdr = *reinterpret_cast<const keybag_hdr_t *>(data);

	(void)size;

	if (hdr.version != 2)
		return false;

	m_data.assign(data, data + hdr.no_of_bytes);

	return true;
}

size_t Keybag::GetKeyCnt()
{
	if (m_data.empty())
		return 0;

	const keybag_hdr_t &hdr = *reinterpret_cast<const keybag_hdr_t *>(m_data.data());

	return hdr.no_of_keys;
}

bool Keybag::GetKey(size_t nr, key_data_t & keydata)
{
	if (m_data.empty())
		return false;

	const keybag_hdr_t &hdr = *reinterpret_cast<const keybag_hdr_t *>(m_data.data());

	if (nr >= hdr.no_of_keys)
		return false;

	const uint8_t *ptr;
	const key_hdr_t *khdr;
	size_t len;
	size_t k;

	ptr = m_data.data() + 0x10;

	for (k = 0; k < nr; k++)
	{
		khdr = reinterpret_cast<const key_hdr_t *>(ptr);

		len = (khdr->length + sizeof(key_hdr_t) + 0xF) & ~0xF;
		ptr += len;
	}

	khdr = reinterpret_cast<const key_hdr_t *>(ptr);

	keydata.header = khdr;
	keydata.data.data = ptr + sizeof(key_hdr_t);
	keydata.data.size = khdr->length;

	return true;
}

bool Keybag::FindKey(const apfs_uuid_t & uuid, uint16_t type, key_data_t & keydata)
{
	if (m_data.empty())
		return false;

	const keybag_hdr_t &hdr = *reinterpret_cast<const keybag_hdr_t *>(m_data.data());

	const uint8_t *ptr;
	const key_hdr_t *khdr;
	size_t len;
	size_t k;

	ptr = m_data.data() + 0x10;

	for (k = 0; k < hdr.no_of_keys; k++)
	{
		khdr = reinterpret_cast<const key_hdr_t *>(ptr);

		if (memcmp(uuid, khdr->uuid, sizeof(apfs_uuid_t)) == 0 && khdr->type == type)
		{
			keydata.header = khdr;
			keydata.data.data = ptr + sizeof(key_hdr_t);
			keydata.data.size = khdr->length;

			return true;
		}

		len = (khdr->length + sizeof(key_hdr_t) + 0xF) & ~0xF;
		ptr += len;
	}

	return false;
}

std::string hexstr(const uint8_t *data, size_t size)
{
	using namespace std;

	std::ostringstream st;

	size_t k;

	st << hex << uppercase << setfill('0');

	for (k = 0; k < size; k++)
		st << setw(2) << static_cast<unsigned int>(data[k]);

	return st.str();
}

#undef DUMP_RAW_KEYS

void Keybag::dump(std::ostream &st, Keybag *cbag, const apfs_uuid_t &vuuid)
{
	using namespace std;

	(void)vuuid;

	size_t s;
	size_t k;
	key_data_t kd;
	blob_header_t bhdr;
	const char *typestr;

	st << "Dumping Keybag (" << (cbag ? "recs" : "keys") << ")" << endl;
	st << endl;

	if (m_data.empty())
	{
		st << "Keybag is empty." << endl;
		return;
	}

#ifdef DUMP_RAW_KEYS
	DumpHex(st, m_data.data(), m_data.size());
	st << endl;
#endif

	const keybag_hdr_t &hdr = *reinterpret_cast<const keybag_hdr_t *>(m_data.data());

	st << "Version : " << setw(4) << hdr.version << endl;
	st << "Keys    : " << setw(4) << hdr.no_of_keys << endl;
	st << "Bytes   : " << setw(8) << hdr.no_of_bytes << endl;
	st << endl;

	s = GetKeyCnt();

	for (k = 0; k < s; k++)
	{
		GetKey(k, kd);

		typestr = "!!! Unknown !!!";

		if (!cbag)
		{
			switch (kd.header->type)
			{
				case 2: typestr = "VEK"; break;
				case 3: typestr = "Keybag Ref"; break;
				default: break;
			}
		}
		else
		{
			switch (kd.header->type)
			{
				case 3: typestr = "KEK"; break;
				case 4: typestr = "Password Hint"; break;
				default: break;
			}
		}

		st << "Key " << k << ":" << endl;
		st << "UUID    : " << uuidstr(kd.header->uuid) << endl;
		st << "Type    : " << setw(4) << kd.header->type << " [" << typestr << "]" << endl;
		st << "Length  : " << setw(4) << kd.header->length << endl;
		st << "Unknown : " << setw(8) << kd.header->unknown << endl;
		// DumpHex(st, kd.data.data, kd.data.size);
		st << endl;

		if (!cbag)
		{
			switch (kd.header->type)
			{
			case 2:
				if (KeyManager::DecodeBlobHeader(bhdr, kd.data))
				{
					st << "[Blob Header]" << endl;
					st << "Unk 80  : " << bhdr.unk_80 << endl;
					st << "HMAC    : " << hexstr(bhdr.hmac, sizeof(bhdr.hmac)) << endl;
					st << "Salt    : " << hexstr(bhdr.salt, sizeof(bhdr.salt)) << endl;
#ifdef DUMP_RAW_KEYS
					st << "Data    :" << endl;
					DumpHex(st, bhdr.blob.data, bhdr.blob.size);
#endif
					st << endl;

					vek_blob_t vek;

					if (KeyManager::DecodeVEKBlob(vek, bhdr.blob))
					{
						st << "[VEK]" << endl;
						st << "Unk 80  : " << vek.unk_80 << endl;
						st << "UUID    : " << uuidstr(vek.uuid) << endl;
						st << "Unk 82  : " << setw(8) << vek.unk_82.unk_00 << ' ' << setw(4) << vek.unk_82.unk_04 << ' ';
						st << setw(2) << static_cast<int>(vek.unk_82.unk_06) << ' ' << setw(2) << static_cast<int>(vek.unk_82.unk_07) << endl;
						st << "VEK Wrpd: " << hexstr(vek.wrapped_vek, sizeof(vek.wrapped_vek)) << endl;
						st << endl;
					}
					else
					{
						st << "Invalid VEK Blob!!!" << endl;
					}
				}
				else
				{
					st << "Invalid BLOB Header!!!" << endl;
				}
				break;
			case 3:
				{
					const key_extent_t *ext = reinterpret_cast<const key_extent_t *>(kd.data.data);
					st << "Block   : " << setw(16) << ext->blk << endl;
					st << "Count   : " << setw(16) << ext->bcnt << endl;
				}
				break;
			default:
				st << "Unknown Type !!!" << endl;
				break;
			}
		}
		else
		{
			switch (kd.header->type)
			{
			case 3:
				if (KeyManager::DecodeBlobHeader(bhdr, kd.data))
				{
					st << "[Blob Header]" << endl;
					st << "Unk 80  : " << bhdr.unk_80 << endl;
					st << "HMAC    : " << hexstr(bhdr.hmac, sizeof(bhdr.hmac)) << endl;
					st << "Salt    : " << hexstr(bhdr.salt, sizeof(bhdr.salt)) << endl;
#ifdef DUMP_RAW_KEYS
					st << "Data    :" << endl;
					DumpHex(st, bhdr.blob.data, bhdr.blob.size);
#endif
					st << endl;

					kek_blob_t kek;

					if (KeyManager::DecodeKEKBlob(kek, bhdr.blob))
					{
						st << "[KEK]" << endl;
						st << "Unk 80  : " << kek.unk_80 << endl;
						st << "UUID    : " << uuidstr(kek.uuid) << endl;
						st << "Unk 82  : " << setw(8) << kek.unk_82.unk_00 << ' ' << setw(4) << kek.unk_82.unk_04 << ' ';
						st << setw(2) << static_cast<int>(kek.unk_82.unk_06) << ' ' << setw(2) << static_cast<int>(kek.unk_82.unk_07) << endl;
						st << "KEK Wrpd: " << hexstr(kek.wrapped_kek, sizeof(kek.wrapped_kek)) << endl;
						st << "Iterat's: " << dec << kek.iterations << hex << endl;
						st << "Salt    : " << hexstr(kek.salt, sizeof(kek.salt)) << endl;
						st << endl;

#if 0 // Test decryption of keys
						string pw;
						uint8_t dk[0x20] = { 0 };
						uint8_t kekk[0x20] = { 0 };
						uint64_t iv;

						cout << "Enter Password for KEK " << uuidstr(kd.header->uuid) << endl;
						GetPassword(pw);

						st << "[Decryption Check]" << endl;
						PBKDF2_HMAC_SHA256(reinterpret_cast<const uint8_t *>(pw.c_str()), pw.size(), kek.salt, sizeof(kek.salt), kek.iterations, dk, sizeof(dk));
						st << "PW DKey : " << hexstr(dk, sizeof(dk)) << endl;
						if (kek.unk_82.unk_00 == 0 || kek.unk_82.unk_00 == 0x10)
							Rfc3394_KeyUnwrap(kekk, kek.wrapped_kek, 0x20, dk, AES::AES_256, &iv);
						else if (kek.unk_82.unk_00 == 2)
							Rfc3394_KeyUnwrap(kekk, kek.wrapped_kek, 0x10, dk, AES::AES_128, &iv);
						else
						{
							st << "kek/82/00 = " << kek.unk_82.unk_00 << " ???" << endl;
							iv = 0;
						}

						st << "KEK Dec : " << hexstr(kekk, sizeof(kekk)) << endl;
						st << "IV KEK  : " << setw(16) << iv << " [" << (iv == 0xA6A6A6A6A6A6A6A6 ? "Correct" : "!!!INCORRECT!!!") << "]" << endl;

						if (iv != 0xA6A6A6A6A6A6A6A6)
							cerr << "Warning: Wrong password." << endl;
						else
						{
							vek_blob_t vek;
							uint8_t vekk[0x20] = { 0 };

							if (cbag->FindKey(vuuid, 2, kd))
							{
								if (KeyManager::DecodeBlobHeader(bhdr, kd.data))
								{
									if (KeyManager::DecodeVEKBlob(vek, bhdr.blob))
									{
										if (vek.unk_82.unk_00 == 0)
											Rfc3394_KeyUnwrap(vekk, vek.wrapped_vek, 0x20, kekk, AES::AES_256, &iv);
										else if (vek.unk_82.unk_00 == 2)
											Rfc3394_KeyUnwrap(vekk, vek.wrapped_vek, 0x10, kekk, AES::AES_128, &iv);
										else
										{
											st << "vek/82/00 = " << vek.unk_82.unk_00 << " ???" << endl;
											iv = 0;
										}

										st << "VEK Dec : " << hexstr(vekk, sizeof(vekk)) << endl;
										st << "IV VEK  : " << setw(16) << iv << " [" << (iv == 0xA6A6A6A6A6A6A6A6 ? "Correct" : "!!!INCORRECT!!!") << "]" << endl;
										if (iv != 0xA6A6A6A6A6A6A6A6)
											cerr << "Error: Password correct, but KEK can't decrypt VEK ..." << endl;
									}
								}
							}
						}
#endif
					}
					else
					{
						st << "Invalid KEK Blob!!!" << endl;
					}
				}
				else
				{
					st << "Invalid BLOB Header!!!" << endl;
				}
				break;
			case 4:
				st << "Hint    : " << string(reinterpret_cast<const char *>(kd.data.data), kd.data.size) << endl;
				break;
			}
		}

		st << endl;
	}

	st << endl;
}

KeyManager::KeyManager(ApfsContainer& container) : m_container(container)
{
	memset(m_container_uuid, 0, sizeof(m_container_uuid));
	m_is_valid = false;
}

KeyManager::~KeyManager()
{
}

bool KeyManager::Init(uint64_t block, uint64_t blockcnt, const apfs_uuid_t& container_uuid)
{
	bool rc;

	rc = LoadKeybag(m_container_bag, 0x6B657973, block, blockcnt, container_uuid);
	if (rc)
		memcpy(m_container_uuid, container_uuid, sizeof(apfs_uuid_t));
	else
		memset(m_container_uuid, 0, sizeof(m_container_uuid));
	m_is_valid = rc;
	return rc;
}

bool KeyManager::GetPasswordHint(std::string& hint, const apfs_uuid_t& volume_uuid)
{
	key_data_t recs_block;
	key_data_t hint_data;

	if (!m_container_bag.FindKey(volume_uuid, 3, recs_block))
		return false;

	if (recs_block.data.size != sizeof(key_extent_t))
		return false;

	const key_extent_t &recs_ext = *reinterpret_cast<const key_extent_t *>(recs_block.data.data);

	Keybag recs_bag;

	if (!LoadKeybag(recs_bag, 0x72656373, recs_ext.blk, recs_ext.bcnt, volume_uuid))
		return false;

	if (!recs_bag.FindKey(volume_uuid, 4, hint_data))
		return false;

	hint.assign(reinterpret_cast<const char *>(hint_data.data.data), hint_data.data.size);

	return true;
}

bool KeyManager::GetVolumeKey(uint8_t* vek, const apfs_uuid_t& volume_uuid, const char* password)
{
	key_data_t recs_block;
	key_data_t kek_header;
	key_data_t vek_header;
	bagdata_t kek_data;
	bagdata_t vek_data;
	kek_blob_t kek_blob;
	vek_blob_t vek_blob;

	if (g_debug & Dbg_Crypto)
	{
		std::cout.setf(std::ios::hex | std::ios::uppercase);
		std::cout.fill('0');

		m_container_bag.dump(std::cout, nullptr, volume_uuid);
	}

	if (!m_container_bag.FindKey(volume_uuid, 3, recs_block))
		return false;

	if (recs_block.data.size != sizeof(key_extent_t))
		return false;

	const key_extent_t &recs_ext = *reinterpret_cast<const key_extent_t *>(recs_block.data.data);

	Keybag recs_bag;

	if (!LoadKeybag(recs_bag, 0x72656373, recs_ext.blk, recs_ext.bcnt, volume_uuid))
		return false;

	if (g_debug & Dbg_Crypto)
		recs_bag.dump(std::cout, &m_container_bag, volume_uuid);

	uint8_t dk[0x20];
	uint8_t kek[0x20] = { 0 };
	uint64_t iv;
	bool rc = false;

	auto cnt = recs_bag.GetKeyCnt();
	decltype(cnt) k;

	// Check all KEKs for any valid KEK.
	for (k = 0; k < cnt; k++)
	{
		if (!recs_bag.GetKey(k, kek_header))
			continue;

		if (kek_header.header->type != 3)
			continue;

		if (!VerifyBlob(kek_header.data, kek_data))
			continue;

		if (!DecodeKEKBlob(kek_blob, kek_data))
			continue;

		assert(kek_blob.iterations <= INT_MAX);
		PBKDF2_HMAC_SHA256(reinterpret_cast<const uint8_t *>(password), strlen(password), kek_blob.salt, sizeof(kek_blob.salt), int(kek_blob.iterations), dk, sizeof(dk));

		switch (kek_blob.unk_82.unk_00)
		{
		case 0x00:
		case 0x10:
			rc = Rfc3394_KeyUnwrap(kek, kek_blob.wrapped_kek, 0x20, dk, AES::AES_256, &iv);
			break;
		case 0x02:
			rc = Rfc3394_KeyUnwrap(kek, kek_blob.wrapped_kek, 0x10, dk, AES::AES_128, &iv);
			break;
		default:
			std::cerr << "Unknown KEK key flags 82/00 = " << std::hex << kek_blob.unk_82.unk_00 << ". Please file a bug report." << std::endl;
			rc = false;
			iv = 0; // to silence warning about iv being used uninitialized.
			break;
		}

		if (g_debug & Dbg_Crypto)
		{
			std::cout << "PW Key  : " << hexstr(dk, sizeof(dk)) << std::endl;
			std::cout << "KEK Wrpd: " << hexstr(kek_blob.wrapped_kek, sizeof(kek_blob.wrapped_kek)) << std::endl;
			std::cout << "KEK     : " << hexstr(kek, 0x20) << std::endl;
			std::cout << "KEK IV  : " << std::setw(16) << iv << std::endl;
			std::cout << std::endl;
		}

		if (rc)
			break;
	}

	if (!rc)
	{
		if (g_debug & Dbg_Crypto)
			std::cout << "Password doesn't work for any key." << std::endl;
		return false;
	}

	if (!m_container_bag.FindKey(volume_uuid, 2, vek_header))
		return false;

	if (!VerifyBlob(vek_header.data, vek_data))
		return false;

	if (!DecodeVEKBlob(vek_blob, vek_data))
		return false;

	memset(vek, 0, 0x20);

	if (vek_blob.unk_82.unk_00 == 0)
	{
		// AES-256. This method is used for wrapping the whole XTS-AES key,
		// and applies to non-FileVault encrypted APFS volumes.
		rc = Rfc3394_KeyUnwrap(vek, vek_blob.wrapped_vek, 0x20, kek, AES::AES_256, &iv);
	}
	else if (vek_blob.unk_82.unk_00 == 2)
	{
		// AES-128. This method is used for FileVault and CoreStorage encrypted
		// volumes that have been converted to APFS.
		rc = Rfc3394_KeyUnwrap(vek, vek_blob.wrapped_vek, 0x10, kek, AES::AES_128, &iv);

		if (rc)
		{
			SHA256 sha;
			uint8_t sha_result[0x20];
			sha.Init();

			// Use (VEK || vek_blob.uuid), then SHA256, then take the first 16 bytes
			sha.Update(vek, 0x10);
			sha.Update(vek_blob.uuid, 0x10);
			sha.Final(sha_result);
			memcpy(vek + 0x10, sha_result, 0x10);
		}
	}
	else
	{
		// Unknown method.
		std::cerr << "Unknown VEK key flags 82/00 = " << std::hex << vek_blob.unk_82.unk_00 << ". Please file a bug report." << std::endl;
		rc = false;
		iv = 0; // to silence the uninitialized warning
	}

	if (g_debug & Dbg_Crypto)
	{
		std::cout << "VEK Wrpd: " << hexstr(vek_blob.wrapped_vek, 0x28) << std::endl;
		std::cout << "VEK     : " << hexstr(vek, 0x20) << std::endl;
		std::cout << "VEK IV  : " << std::setw(16) << iv << std::endl;
	}

	return rc;
}

void KeyManager::dump(std::ostream &st)
{
	size_t k;
	size_t s;
	key_data_t d;
	apfs_uuid_t dummy_uuid;
	std::ios::fmtflags fl = st.setf(st.hex | st.uppercase);
	char ch = st.fill('0');

	memset(dummy_uuid, 0, sizeof(dummy_uuid));

	m_container_bag.dump(st, nullptr, dummy_uuid);

	s = m_container_bag.GetKeyCnt();

	for (k = 0; k < s; k++)
	{
		if (!m_container_bag.GetKey(k, d))
			continue;

		if ((d.header->type == 3) && (d.header->length == sizeof(key_extent_t)))
		{
			const key_extent_t &ext = *reinterpret_cast<const key_extent_t *>(d.data.data);
			Keybag recs_bag;

			if (LoadKeybag(recs_bag, 0x72656373, ext.blk, ext.bcnt, d.header->uuid))
				recs_bag.dump(st, &m_container_bag, d.header->uuid);
		}
	}

	st << std::endl;
	st << "===========================================================================================================================" << std::endl;
	st << std::endl;

	st.fill(ch);
	st.setf(fl);
}

bool KeyManager::LoadKeybag(Keybag& bag, uint32_t type, uint64_t block, uint64_t blockcnt, const apfs_uuid_t& uuid)
{
	std::vector<uint8_t> data;
	size_t k;
	const size_t blocksize = m_container.GetBlocksize();

	if (g_debug & Dbg_Crypto)
		std::cout << "starting LoadKeybag" << std::endl;

	data.resize(blockcnt * blocksize);

	m_container.ReadBlocks(data.data(), block, blockcnt);

	DecryptBlocks(data.data(), block, blockcnt, uuid);

	for (k = 0; k < blockcnt; k++)
	{
		if (!VerifyBlock(data.data(), blockcnt * blocksize))
			return false;
	}

	if (g_debug & Dbg_Crypto)
		std::cout << " all blocks verified" << std::endl;

	const APFS_ObjHeader &hdr = *reinterpret_cast<const APFS_ObjHeader *>(data.data());

	if (hdr.o_type != type)
	{
		if (g_debug & Dbg_Errors)
		{
			std::cout << "Keybag block types not matching: " << hdr.o_type << ", expected " << type << std::endl;
			DumpHex(std::cout, data.data(), data.size());
		}

		return false;
	}

	bag.Init(data.data() + sizeof(APFS_ObjHeader), data.size() - sizeof(APFS_ObjHeader)); // TODO: This only works with one block ...

	return true;
}

void KeyManager::DecryptBlocks(uint8_t* data, uint64_t block, uint64_t cnt, const uint8_t* key)
{
	AesXts xts;
	size_t k;
	size_t size;
	uint64_t uno;
	uint64_t cs_factor = m_container.GetBlocksize() / 0x200;

	xts.SetKey(key, key);

	uno = block * cs_factor;
	size = m_container.GetBlocksize() * cnt;

	for (k = 0; k < size; k += 0x200)
	{
		xts.Decrypt(data + k, data + k, 0x200, uno);
		uno++;
	}
}

bool KeyManager::VerifyBlob(const bagdata_t & keydata, bagdata_t & contents)
{
	static const uint8_t blob_cookie[6] = { 0x01, 0x16, 0x20, 0x17, 0x15, 0x05 };

	KeyParser parser;
	blob_header_t bhdr;
	SHA256 sha;
	uint8_t hmac_key[0x20];
	uint8_t hmac_calc[0x20];

	if (!DecodeBlobHeader(bhdr, keydata))
		return false;

	sha.Init();
	sha.Update(blob_cookie, 6);
	sha.Update(bhdr.salt, 8);
	sha.Final(hmac_key);

	HMAC_SHA256(hmac_key, 0x20, bhdr.blob.data, bhdr.blob.size, hmac_calc);

	if (memcmp(bhdr.hmac, hmac_calc, 0x20) != 0)
		return false;

	contents = bhdr.blob;

	return true;
}

bool KeyManager::DecodeBlobHeader(blob_header_t & hdr, const bagdata_t & data)
{
	KeyParser parser;
	bagdata_t blob_hdr;

	parser.SetData(data);

	if (!parser.GetAny(0x30, blob_hdr))
		return false;

	parser.SetData(blob_hdr);

	if (!parser.GetUInt64(0x80, hdr.unk_80))
		return false;

	if (!parser.GetBytes(0x81, hdr.hmac, 0x20))
		return false;

	if (!parser.GetBytes(0x82, hdr.salt, 0x08))
		return false;

	parser.GetRemaining(hdr.blob);

	return true;
}

bool KeyManager::DecodeKEKBlob(kek_blob_t & kek_blob, const bagdata_t & data)
{
	KeyParser parser;
	bagdata_t key_hdr;

	parser.SetData(data);

	if (!parser.GetAny(0xA3, key_hdr))
		return false;

	parser.SetData(key_hdr);

	if (!parser.GetUInt64(0x80, kek_blob.unk_80))
		return false;

	if (!parser.GetBytes(0x81, kek_blob.uuid, 0x10))
		return false;

	if (!parser.GetBytes(0x82, reinterpret_cast<uint8_t *>(&kek_blob.unk_82), 8))
		return false;

	if (!parser.GetBytes(0x83, kek_blob.wrapped_kek, 0x28))
		return false;

	if (!parser.GetUInt64(0x84, kek_blob.iterations))
		return false;

	if (!parser.GetBytes(0x85, kek_blob.salt, 0x10))
		return false;

	return true;
}

bool KeyManager::DecodeVEKBlob(vek_blob_t & vek_blob, const bagdata_t & data)
{
	KeyParser parser;
	bagdata_t key_hdr;

	parser.SetData(data);

	if (!parser.GetAny(0xA3, key_hdr))
		return false;

	parser.SetData(key_hdr);

	if (!parser.GetUInt64(0x80, vek_blob.unk_80))
		return false;

	if (!parser.GetBytes(0x81, vek_blob.uuid, 0x10))
		return false;

	if (!parser.GetBytes(0x82, reinterpret_cast<uint8_t *>(&vek_blob.unk_82), 8))
		return false;

	if (!parser.GetBytes(0x83, vek_blob.wrapped_vek, 0x28))
		return false;

	return true;
}

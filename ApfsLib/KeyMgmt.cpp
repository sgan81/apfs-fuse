#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "Container.h"
#include <Crypto/Asn1Der.h>
#include <Crypto/AesXts.h>
#include <Crypto/Sha256.h>
#include <Crypto/Crypto.h>
#include "Util.h"
#include "BlockDumper.h"

#include "KeyMgmt.h"

struct bagdata_t
{
	const uint8_t *data;
	size_t size;
};

struct key_info_t
{
	uint32_t flags;
	uint8_t unk_04;
	uint8_t unk_05;
	uint8_t uuid[16];
};

struct key_header_t
{
	uint64_t unk_0;
	apfs_uuid_t uuid;
	key_info_t info;
};

struct kek_entry_t
{
	key_header_t hdr;
	uint8_t wrapped_kek[0x28];
	uint64_t iterations;
	uint8_t salt[0x10];
};

struct vek_entry_t
{
	key_header_t hdr;
	uint8_t wrapped_vek[0x28];
};

Keybag::Keybag()
{
	m_kl = nullptr;
}

Keybag::~Keybag()
{
}

bool Keybag::Init(const media_keybag_t * mk, size_t size)
{
	(void)size;

	if (mk->mk_locker.kl_version != 2)
		return false;

	const uint8_t *data = reinterpret_cast<const uint8_t *>(&mk->mk_locker);

	m_data.assign(data, data + mk->mk_locker.kl_nbytes);
	m_kl = reinterpret_cast<kb_locker_t *>(m_data.data());

	return true;
}

size_t Keybag::GetKeyCnt()
{
	if (m_kl)
		return m_kl->kl_nkeys;
	else
		return 0;
}

const keybag_entry_t * Keybag::GetKey(size_t nr)
{
	if (!m_kl)
		return nullptr;

	if (nr >= m_kl->kl_nkeys)
		return nullptr;

	const uint8_t *ptr;
	const keybag_entry_t *kb;
	size_t len;
	size_t k;

	ptr = m_kl->kl_entries;

	for (k = 0; k < nr; k++)
	{
		kb = reinterpret_cast<const keybag_entry_t *>(ptr);

		len = (kb->ke_keylen + sizeof(keybag_entry_t) + 0x0F) & ~0xF;
		ptr += len;
	}

	kb = reinterpret_cast<const keybag_entry_t *>(ptr);

	return kb;
}

const keybag_entry_t * Keybag::FindKey(const apfs_uuid_t & uuid, uint16_t type)
{
	if (!m_kl)
		return nullptr;

	const uint8_t *ptr;
	const keybag_entry_t *kb;
	size_t len;
	size_t k;

	ptr = m_kl->kl_entries;

	for (k = 0; k < m_kl->kl_nkeys; k++)
	{
		kb = reinterpret_cast<const keybag_entry_t *>(ptr);

		if (memcmp(uuid, kb->ke_uuid, sizeof(apfs_uuid_t)) == 0 && kb->ke_tag == type)
			return kb;

		len = (kb->ke_keylen + sizeof(keybag_entry_t) + 0x0F) & ~0xF;
		ptr += len;
	}

	return nullptr;
}

#undef DUMP_RAW_KEYS

void Keybag::dump(std::ostream &st, Keybag *cbag, const apfs_uuid_t &vuuid)
{
	using namespace std;

	(void)vuuid;

	size_t s;
	size_t k;
	const keybag_entry_t *ke;
	bagdata_t bd;
	const char *typestr;

	st << "Dumping Keybag (" << (cbag ? "recs" : "keys") << ")" << endl;
	st << endl;

	if (m_data.empty() || !m_kl)
	{
		st << "Keybag is empty." << endl;
		return;
	}

#ifdef DUMP_RAW_KEYS
	DumpHex(st, m_data.data(), m_data.size());
	st << endl;
#endif

	st << "Version : " << setw(4) << m_kl->kl_version << endl;
	st << "Keys    : " << setw(4) << m_kl->kl_nkeys << endl;
	st << "Bytes   : " << setw(8) << m_kl->kl_nbytes << endl;
	st << endl;

	s = GetKeyCnt();

	for (k = 0; k < s; k++)
	{
		ke = GetKey(k);

		typestr = "!!! Unknown !!!";

		if (!cbag)
		{
			switch (ke->ke_tag)
			{
				case KB_TAG_VOLUME_KEY: typestr = "VEK"; break;
				case KB_TAG_VOLUME_UNLOCK_RECORDS: typestr = "Keybag Ref"; break;
				default: break;
			}
		}
		else
		{
			switch (ke->ke_tag)
			{
				case KB_TAG_VOLUME_UNLOCK_RECORDS: typestr = "KEK"; break;
				case KB_TAG_VOLUME_PASSPHRASE_HINT: typestr = "Password Hint"; break;
				default: break;
			}
		}

		st << "Key " << k << ":" << endl;
		st << "UUID    : " << uuidstr(ke->ke_uuid) << endl;
		st << "Type    : " << setw(4) << ke->ke_tag << " [" << typestr << "]" << endl;
		st << "Length  : " << setw(4) << ke->ke_keylen << endl;
		/* Skip the ke->_padding_ */

		// DumpHex(st, kd.data.data, kd.data.size);
#ifdef DUMP_RAW_KEYS
		DumpHex(st, ke->ke_keydata, ke->ke_keylen);
		st << endl;
#endif

		if (!cbag)
		{
			switch (ke->ke_tag)
			{
			case 2:
				der_dump(ke->ke_keydata, ke->ke_keylen);
#if 0
				bd.data = ke->ke_keydata;
				bd.size = ke->ke_keylen;

				if (KeyManager::DecodeBlobHeader(bhdr, bd))
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
#endif
				break;
			case 3:
				{
					const prange_t *pr = reinterpret_cast<const prange_t *>(ke->ke_keydata);
					st << "Block   : " << setw(16) << pr->pr_start_addr << endl;
					st << "Count   : " << setw(16) << pr->pr_block_count << endl;
				}
				break;
			default:
				st << "Unknown Type !!!" << endl;
				break;
			}
		}
		else
		{
			switch (ke->ke_tag)
			{
			case 3:
				der_dump(ke->ke_keydata, ke->ke_keylen);
#if 0
				bd.data = ke->ke_keydata;
				bd.size = ke->ke_keylen;

				if (KeyManager::DecodeBlobHeader(bhdr, bd))
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
#endif
				break;
			case 4:
				st << "Hint    : " << string(reinterpret_cast<const char *>(ke->ke_keydata), ke->ke_keylen) << endl;
				break;
			}
		}

		st << endl;
	}

	st << endl;
}

KeyManager::KeyManager(Container& container) : m_container(container)
{
	memset(m_container_uuid, 0, sizeof(m_container_uuid));
	m_is_valid = false;
	m_is_unencrypted = false;
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
	const keybag_entry_t *ke;

	ke = m_container_bag.FindKey(volume_uuid, KB_TAG_VOLUME_UNLOCK_RECORDS);
	if (!ke)
		return false;

	if (ke->ke_keylen != sizeof(prange_t))
		return false;

	const prange_t *pr = reinterpret_cast<const prange_t *>(ke->ke_keydata);

	Keybag recs_bag;

	if (!LoadKeybag(recs_bag, APFS_VOL_KEYBAG_OBJ, pr->pr_start_addr, pr->pr_block_count, volume_uuid))
		return false;

	ke = recs_bag.FindKey(volume_uuid, KB_TAG_VOLUME_PASSPHRASE_HINT);
	if (!ke)
		return false;

	hint.assign(reinterpret_cast<const char *>(ke->ke_keydata), ke->ke_keylen);

	return true;
}

bool KeyManager::GetVolumeKey(uint8_t* vek, const apfs_uuid_t& volume_uuid, const char* password)
{
	const keybag_entry_t *ke_recs;

	if (g_debug & Dbg_Crypto)
	{
		std::cout.setf(std::ios::hex | std::ios::uppercase);
		std::cout.fill('0');

		m_container_bag.dump(std::cout, nullptr, volume_uuid);
	}

	ke_recs = m_container_bag.FindKey(volume_uuid, 3);
	if (!ke_recs)
		return false;

	if (ke_recs->ke_keylen != sizeof(prange_t))
		return false;

	const prange_t *pr = reinterpret_cast<const prange_t *>(ke_recs->ke_keydata);

	Keybag recs_bag;

	if (!LoadKeybag(recs_bag, APFS_VOL_KEYBAG_OBJ, pr->pr_start_addr, pr->pr_block_count, volume_uuid))
		return false;

	if (g_debug & Dbg_Crypto)
		recs_bag.dump(std::cout, &m_container_bag, volume_uuid);

	uint8_t dk[0x20] = {};
	uint8_t kek[0x20] = {};
	uint64_t iv;
	bool rc = false;
	const keybag_entry_t *ke_kek;
	const keybag_entry_t *ke_vek;
	bagdata_t bd;
	kek_entry_t keke;
	vek_entry_t veke;
	AES::Mode kek_mode = AES::AES_256;

	int cnt = recs_bag.GetKeyCnt();
	int k;

	// Check all KEKs for any valid KEK.
	for (k = 0; k < cnt; k++)
	{
		ke_kek = recs_bag.GetKey(k);
		if (!ke_kek)
			continue;

		if (ke_kek->ke_tag != KB_TAG_VOLUME_UNLOCK_RECORDS)
			continue;

		if (!DecodeKEK(keke, ke_kek->ke_keydata, ke_kek->ke_keydata + ke_kek->ke_keylen))
			continue;

		PBKDF2_HMAC_SHA256(reinterpret_cast<const uint8_t *>(password), strlen(password), keke.salt, sizeof(keke.salt), keke.iterations, dk, sizeof(dk));

		// There are more variants here ... 1 = hw_crypt
		if (keke.hdr.info.flags & 2) {
			rc = Rfc3394_KeyUnwrap(kek, keke.wrapped_kek, 0x10, dk, AES::AES_128, &iv);
			kek_mode = AES::AES_128;
		} else {
			rc = Rfc3394_KeyUnwrap(kek, keke.wrapped_kek, 0x20, dk, AES::AES_256, &iv);
			kek_mode = AES::AES_256;
		}

		if (g_debug & Dbg_Crypto)
		{
			std::cout << "PW Key  : " << hexstr(dk, sizeof(dk)) << std::endl;
			std::cout << "KEK Wrpd: " << hexstr(keke.wrapped_kek, sizeof(keke.wrapped_kek)) << std::endl;
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

	ke_vek = m_container_bag.FindKey(volume_uuid, KB_TAG_VOLUME_KEY);
	if (!ke_vek)
		return false;

	if (!DecodeVEK(veke, ke_vek->ke_keydata, ke_vek->ke_keydata + ke_vek->ke_keylen))
		return false;

	memset(vek, 0, 0x20);

	// TODO 1
	if (veke.hdr.info.flags & 2) {
		// AES-128. This method is used for FileVault and CoreStorage encrypted
		// volumes that have been converted to APFS.
		rc = Rfc3394_KeyUnwrap(vek, veke.wrapped_vek, 0x10, kek, kek_mode, &iv);

		if (rc)
		{
			SHA256 sha;
			uint8_t sha_result[0x20];
			sha.Init();

			// Use (VEK || vek_blob.uuid), then SHA256, then take the first 16 bytes
			sha.Update(vek, 0x10);
			sha.Update(veke.hdr.uuid, 0x10);
			sha.Final(sha_result);
			memcpy(vek + 0x10, sha_result, 0x10);
		}
	} else {
		// AES-256. This method is used for wrapping the whole XTS-AES key,
		// and applies to non-FileVault encrypted APFS volumes.
		rc = Rfc3394_KeyUnwrap(vek, veke.wrapped_vek, 0x20, kek, kek_mode, &iv);
	}

	if (g_debug & Dbg_Crypto)
	{
		std::cout << "VEK Wrpd: " << hexstr(veke.wrapped_vek, 0x28) << std::endl;
		std::cout << "VEK     : " << hexstr(vek, 0x20) << std::endl;
		std::cout << "VEK IV  : " << std::setw(16) << iv << std::endl;
	}

	return rc;
}

void KeyManager::dump(std::ostream &st)
{
	size_t k;
	size_t s;
	const keybag_entry_t *ke;
	apfs_uuid_t dummy_uuid;
	std::ios::fmtflags fl = st.setf(st.hex | st.uppercase);
	char ch = st.fill('0');

	memset(dummy_uuid, 0, sizeof(dummy_uuid));

	m_container_bag.dump(st, nullptr, dummy_uuid);

	s = m_container_bag.GetKeyCnt();

	for (k = 0; k < s; k++)
	{
		ke = m_container_bag.GetKey(k);
		if (!ke)
			continue;

		if ((ke->ke_tag == KB_TAG_VOLUME_UNLOCK_RECORDS) && (ke->ke_keylen == sizeof(prange_t)))
		{
			st << std::endl;
			st << "---------------------------------------------------------------------------------------------------------------------------" << std::endl;
			st << std::endl;

			const prange_t *pr = reinterpret_cast<const prange_t *>(ke->ke_keydata);
			Keybag recs_bag;

			if (LoadKeybag(recs_bag, APFS_VOL_KEYBAG_OBJ, pr->pr_start_addr, pr->pr_block_count, ke->ke_uuid))
				recs_bag.dump(st, &m_container_bag, ke->ke_uuid);
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
		std::cout << "starting LoadKeybag @ " << std::hex << block << std::endl;

	data.resize(blockcnt * blocksize);
	const media_keybag_t *mk = reinterpret_cast<const media_keybag_t *>(data.data());

	m_container.ReadBlocks(data.data(), block, blockcnt);
	if (mk->mk_obj.o_type == type)
		m_is_unencrypted = true;
	else
		DecryptBlocks(data.data(), block, blockcnt, uuid);

	for (k = 0; k < blockcnt; k++)
	{
		if (!VerifyBlock(data.data(), blockcnt * blocksize))
			return false;
	}

	if (g_debug & Dbg_Crypto)
		std::cout << " all blocks verified" << std::endl;

	if (mk->mk_obj.o_type != type)
	{
		if (g_debug & Dbg_Errors)
		{
			std::cout << "Keybag block types not matching: " << mk->mk_obj.o_type << ", expected " << type << std::endl;
			DumpHex(std::cout, data.data(), data.size());
		}

		return false;
	}

	bag.Init(mk, data.size()); // TODO: This only works with one block ...

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

const uint8_t* KeyManager::DecodeKeyHeader(key_header_t& hdr, const uint8_t*& end, const uint8_t* der, const uint8_t* der_end)
{
	static const uint8_t blob_cookie[6] = { 0x01, 0x16, 0x20, 0x17, 0x15, 0x05 };

	const uint8_t* body_end;
	uint64_t hmac_0;
	uint8_t hmac_hash[32];
	uint8_t hmac_salt[8];
	size_t info_len;

	SHA256 sha256;
	uint8_t hmac_key[32];
	uint8_t hmac_res[40];

	der = der_decode_sequence_tl(body_end, der, der_end);
	if (der == nullptr) return nullptr;
	der = der_decode_uint64(0x8000000000000000U, hmac_0, der, body_end);
	der = der_decode_octet_string_copy(0x8000000000000001U, hmac_hash, 32, der, body_end);
	der = der_decode_octet_string_copy(0x8000000000000002U, hmac_salt, 8, der, body_end);
	if (der == nullptr) {
		log_error("Keybag: Parsing key header failed.\n");
		return nullptr;
	}

	sha256.Init();
	sha256.Update(blob_cookie, 6);
	sha256.Update(hmac_salt, 8);
	sha256.Final(hmac_key);

	HMAC_SHA256(hmac_key, 32, der, body_end - der, hmac_res);
	if (memcmp(hmac_hash, hmac_res, 32) != 0)
		log_error("Keybag: HMAC verification failed.\n");
	// Ignoring for now ...

	der = der_decode_constructed_tl(0xA000000000000003U, end, der, body_end);
	der = der_decode_uint64(0x8000000000000000U, hdr.unk_0, der, end);
	der = der_decode_octet_string_copy(0x8000000000000001U, hdr.uuid, 16, der, end);
	der = der_decode_tl(0x8000000000000002U, info_len, der, end);
	if (info_len > 0x16) return nullptr;
	memcpy(&hdr.info, der, info_len);
	der += info_len;
	hdr.info.flags = bswap_le(hdr.info.flags);
	return der;
}

bool KeyManager::DecodeKEK(kek_entry_t& kek, const uint8_t* der, const uint8_t* der_end)
{
	const uint8_t* end = nullptr;

	memset(&kek, 0, sizeof(kek));
	der = DecodeKeyHeader(kek.hdr, end, der, der_end);
	if (der == nullptr) return false;
	der = der_decode_octet_string_copy(0x8000000000000003U, kek.wrapped_kek, 40, der, end);
	der = der_decode_uint64(0x8000000000000004U, kek.iterations, der, end);
	der = der_decode_octet_string_copy(0x8000000000000005U, kek.salt, 16, der, end);
	return der != nullptr;
}

bool KeyManager::DecodeVEK(vek_entry_t& vek, const uint8_t* der, const uint8_t* der_end)
{
	const uint8_t* end = nullptr;

	memset(&vek, 0, sizeof(vek));
	der = DecodeKeyHeader(vek.hdr, end, der, der_end);
	if (der == nullptr) return false;
	der = der_decode_octet_string_copy(0x8000000000000003U, vek.wrapped_vek, 40, der, der_end);
	// HW encrypt has more here ... TODO
	return der != nullptr;
}

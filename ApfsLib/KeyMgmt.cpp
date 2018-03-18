#include <cstring>

#include "ApfsContainer.h"
#include "AesXts.h"
#include "Sha256.h"
#include "Crypto.h"
#include "Util.h"

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

struct kek_blob_t
{
	uint64_t unk_80;
	apfs_uuid_t uuid;
	uint8_t unk_82[8];
	uint8_t wrapped_kek[0x28];
	uint64_t iterations;
	uint8_t salt[0x10];
};

struct vek_blob_t
{
	uint64_t unk_80;
	apfs_uuid_t uuid;
	uint8_t unk_82[8];
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
	keydata.data.size = hdr.no_of_bytes;

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

	if (!m_container_bag.FindKey(volume_uuid, 3, recs_block))
		return false;

	if (recs_block.data.size != sizeof(key_extent_t))
		return false;

	const key_extent_t &recs_ext = *reinterpret_cast<const key_extent_t *>(recs_block.data.data);

	Keybag recs_bag;

	if (!LoadKeybag(recs_bag, 0x72656373, recs_ext.blk, recs_ext.bcnt, volume_uuid))
		return false;

	if (!recs_bag.FindKey(volume_uuid, 3, kek_header))
		return false;

	if (!VerifyBlob(kek_header.data, kek_data))
		return false;

	if (!DecodeKEKBlob(kek_blob, kek_data))
		return false;

	if (!m_container_bag.FindKey(volume_uuid, 2, vek_header))
		return false;

	if (!VerifyBlob(vek_header.data, vek_data))
		return false;

	if (!DecodeVEKBlob(vek_blob, vek_data))
		return false;

	// PW Check

	uint8_t dk[0x20];
	uint8_t kek[0x20];
	uint64_t iv;

	PBKDF2_HMAC_SHA256(reinterpret_cast<const uint8_t *>(password), strlen(password), kek_blob.salt, sizeof(kek_blob.salt), kek_blob.iterations, dk, sizeof(dk));
	Rfc3394_KeyUnwrap(kek, kek_blob.wrapped_kek, 0x20, dk, AES::AES_256, &iv);

	if (iv != 0xA6A6A6A6A6A6A6A6)
		return false;

	Rfc3394_KeyUnwrap(vek, vek_blob.wrapped_vek, 0x20, kek, AES::AES_256, &iv);

	if (iv != 0xA6A6A6A6A6A6A6A6)
		return false;

	return true;
}

bool KeyManager::LoadKeybag(Keybag& bag, uint32_t type, uint64_t block, uint64_t blockcnt, const apfs_uuid_t& uuid)
{
	std::vector<uint8_t> data;
	size_t k;
	const size_t blocksize = m_container.GetBlocksize();

	data.resize(blockcnt * blocksize);

	m_container.ReadBlocks(data.data(), block, blockcnt);

	DecryptBlocks(data.data(), block, blockcnt, uuid);

	for (k = 0; k < blockcnt; k++)
	{
		if (!VerifyBlock(data.data(), blockcnt * blocksize))
			return false;
	}

	const APFS_BlockHeader &hdr = *reinterpret_cast<const APFS_BlockHeader *>(data.data());

	if (hdr.type != type)
		return false;

	bag.Init(data.data() + sizeof(APFS_BlockHeader), data.size() - sizeof(APFS_BlockHeader)); // TODO: Das funzt nur bei einem Block ...

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

	if (!parser.GetBytes(0x82, kek_blob.unk_82, 8))
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

	if (!parser.GetBytes(0x82, vek_blob.unk_82, 8))
		return false;

	if (!parser.GetBytes(0x83, vek_blob.wrapped_vek, 0x28))
		return false;

	return true;
}

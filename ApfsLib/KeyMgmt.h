#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "Global.h"

class ApfsContainer;

struct bagdata_t;
struct key_hdr_t;
struct key_data_t;
struct keybag_hdr_t;
struct key_extent_t;
struct blob_header_t;
struct kek_blob_t;
struct vek_blob_t;

class KeyParser
{
public:
	KeyParser();

	void SetData(const uint8_t *data, size_t size);
	void SetData(const bagdata_t &data);
	void Rewind();
	void Clear();

	bool GetUInt64(uint8_t expected_tag, uint64_t &result);
	bool GetBytes(uint8_t expected_tag, uint8_t *data, size_t len);
	bool GetAny(uint8_t expected_tag, bagdata_t &data);

	bool GetTagAndLen(uint8_t &tag, size_t &len);

	void GetRemaining(bagdata_t &data);

private:
	inline uint8_t GetByte()
	{
		if (m_ptr < m_end)
			return *m_ptr++;
		else
			return 0;
	}

	const uint8_t *m_start;
	const uint8_t *m_ptr;
	const uint8_t *m_end;
};

class Keybag
{
public:
	Keybag();
	~Keybag();

	bool Init(const uint8_t *data, size_t size);

	size_t GetKeyCnt();
	bool GetKey(size_t nr, key_data_t &keydata);
	bool FindKey(const apfs_uuid_t &uuid, uint16_t type, key_data_t &keydata, int force_index = -1);

	void dump(BlockDumper &bd, Keybag *cbag, const apfs_uuid_t &vuuid);

private:
	std::vector<uint8_t> m_data;
};

class KeyManager
{
	friend class Keybag;
public:
	KeyManager(ApfsContainer &container);
	~KeyManager();

	bool Init(uint64_t block, uint64_t blockcnt, const apfs_uuid_t &container_uuid);

	bool GetPasswordHint(std::string &hint, const apfs_uuid_t &volume_uuid);
	bool GetVolumeKey(uint8_t *vek, const apfs_uuid_t &volume_uuid, const char *password = NULL, bool password_is_prk = false);

	bool IsValid() const { return m_is_valid; }

	void dump(BlockDumper &bd);

private:
	bool LoadKeybag(Keybag &bag, uint32_t type, uint64_t block, uint64_t blockcnt, const apfs_uuid_t &uuid);
	void DecryptBlocks(uint8_t *data, uint64_t block, uint64_t cnt, const uint8_t *key);

	bool VerifyBlob(const bagdata_t &keydata, bagdata_t &contents);

	static bool DecodeBlobHeader(blob_header_t &hdr, const bagdata_t &data);
	static bool DecodeKEKBlob(kek_blob_t &kek_blob, const bagdata_t &data);
	static bool DecodeVEKBlob(vek_blob_t &vek_blob, const bagdata_t &data);

	ApfsContainer &m_container;
	Keybag m_container_bag;
	apfs_uuid_t m_container_uuid;

	bool m_is_valid;
};

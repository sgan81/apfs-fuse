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

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <iosfwd>

#include "Global.h"

class Container;

struct bagdata_t;

struct key_header_t;
struct kek_entry_t;
struct vek_entry_t;

class Keybag
{
public:
	Keybag();
	~Keybag();

	bool Init(const media_keybag_t *mk, size_t size);

	size_t GetKeyCnt();
	const keybag_entry_t * GetKey(size_t nr);
	const keybag_entry_t * FindKey(const apfs_uuid_t &uuid, uint16_t type);

	void dump(std::ostream &st, Keybag *cbag, const apfs_uuid_t &vuuid);

private:
	std::vector<uint8_t> m_data;
	kb_locker_t *m_kl;
};

class KeyManager
{
	friend class Keybag;
public:
	KeyManager(Container &container);
	~KeyManager();

	bool Init(uint64_t block, uint64_t blockcnt, const apfs_uuid_t &container_uuid);

	bool GetPasswordHint(std::string &hint, const apfs_uuid_t &volume_uuid);
	bool GetVolumeKey(uint8_t *vek, const apfs_uuid_t &volume_uuid, const char *password = nullptr);

	bool IsValid() const { return m_is_valid; }
	bool IsUnencrypted() const { return m_is_unencrypted; }

	void dump(std::ostream &st);

private:
	bool LoadKeybag(Keybag &bag, uint32_t type, uint64_t block, uint64_t blockcnt, const apfs_uuid_t &uuid);
	void DecryptBlocks(uint8_t *data, uint64_t block, uint64_t cnt, const uint8_t *key);

	static const uint8_t* DecodeKeyHeader(key_header_t &hdr, const uint8_t*& end, const uint8_t* der, const uint8_t* der_end);
	static bool DecodeKEK(kek_entry_t &kek, const uint8_t* der, const uint8_t* der_end);
	static bool DecodeVEK(vek_entry_t &vek, const uint8_t* der, const uint8_t* der_end);

	Container &m_container;
	Keybag m_container_bag;
	apfs_uuid_t m_container_uuid;

	bool m_is_valid;
	bool m_is_unencrypted;
};

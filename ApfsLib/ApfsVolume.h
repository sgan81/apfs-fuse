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

#pragma once

#include <cstdint>

#include "DiskStruct.h"
#include "ApfsNodeMapperBTree.h"
#include "BTree.h"
#include "AesXts.h"

class ApfsContainer;
class BlockDumper;

class ApfsVolume
{
public:
	ApfsVolume(ApfsContainer &container);
	~ApfsVolume();

	bool Init(uint64_t blkid_volhdr);

	const char *name() const { return reinterpret_cast<const char *>(m_sb.apfs_volname); }

	void dump(BlockDumper &bd);

	BTree &getDirectory() { return m_bt_directory; }
	uint32_t getTextFormat() const { return m_sb.apfs_incompatible_features & 0x9; }

	ApfsContainer &getContainer() const { return m_container; }

	bool ReadBlocks(byte_t *data, uint64_t blkid, uint64_t blkcnt, bool decrypt, uint64_t xts_blkid);

private:
	ApfsContainer &m_container;

	apfs_superblock_t m_sb;

	ApfsNodeMapperBTree m_nodemap_dir;
	BTree m_bt_directory;
	BTree m_bt_blockmap;
	BTree m_bt_snapshots;

	uint64_t m_blockid_sb;

	bool m_is_encrypted;
	AesXts m_aes;
};

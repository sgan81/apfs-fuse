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
#include <Crypto/AesXts.h>

class ApfsContainer;
class BlockDumper;

int CompareStdDirKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);
int CompareFextKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);
int CompareSnapMetaKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);

class ApfsVolume
{
public:
	ApfsVolume(ApfsContainer &container);
	~ApfsVolume();

	bool Init(paddr_t apsb_paddr);
	bool MountSnapshot(paddr_t apsb_paddr, xid_t snap_xid);

	const char *name() const { return reinterpret_cast<const char *>(m_sb.apfs_volname); }

	void dump(BlockDumper &bd);

	BTree &fstree() { return m_fs_tree; }
	BTree &fexttree() { return m_fext_tree; }
	uint32_t getTextFormat() const { return m_sb.apfs_incompatible_features & 0x9; }

	ApfsContainer &getContainer() const { return m_container; }

	bool ReadBlocks(uint8_t *data, paddr_t paddr, uint64_t blkcnt, uint64_t xts_tweak);
	bool isSealed() const { return (m_sb.apfs_incompatible_features & APFS_INCOMPAT_SEALED_VOLUME) != 0; }

private:
	ApfsContainer &m_container;

	apfs_superblock_t m_sb;

	ApfsNodeMapperBTree m_omap;
	BTree m_fs_tree;
	BTree m_extentref_tree;
	BTree m_snap_meta_tree;
	BTree m_fext_tree;

	paddr_t m_apsb_paddr;

	bool m_is_encrypted;
	AesXts m_aes;
};

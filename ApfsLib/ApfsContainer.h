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

#include "Global.h"
#include "BTree.h"
#include "DiskStruct.h"
#include "Device.h"
#include "CheckPointMap.h"
#include "ApfsNodeMapperBTree.h"
#include "KeyMgmt.h"

#include <cstdint>
#include <vector>

class ApfsVolume;
class BlockDumper;

class ApfsContainer
{
public:
	ApfsContainer(Device *disk_main, uint64_t main_start, uint64_t main_len, Device *disk_tier2 = 0, uint64_t tier2_start = 0, uint64_t tier2_len = 0);
	~ApfsContainer();

	bool Init();

	ApfsVolume *GetVolume(unsigned int index, const std::string &passphrase = std::string());
	unsigned int GetVolumeCnt() const;
	bool GetVolumeInfo(unsigned int fsid, apfs_superblock_t &apsb);

	bool ReadBlocks(uint8_t *data, paddr_t paddr, uint64_t blkcnt = 1) const;
	bool ReadAndVerifyHeaderBlock(uint8_t *data, paddr_t paddr) const;

	uint32_t GetBlocksize() const { return m_nx.nx_block_size; }
	uint64_t GetBlockCount() const { return m_nx.nx_block_count; }
	uint64_t GetFreeBlocks() const { return m_sm->sm_dev[SD_MAIN].sm_free_count + m_sm->sm_dev[SD_TIER2].sm_free_count; }

	bool GetVolumeKey(uint8_t *key, const apfs_uuid_t &vol_uuid, const char *password = nullptr);
	bool GetPasswordHint(std::string &hint, const apfs_uuid_t &vol_uuid);

	void dump(BlockDumper& bd);

private:
	Device *m_main_disk;
	const uint64_t m_main_part_start;
	const uint64_t m_main_part_len;

	Device *m_tier2_disk;
	const uint64_t m_tier2_part_start;
	const uint64_t m_tier2_part_len;

	std::string m_passphrase;

	nx_superblock_t m_nx;

	CheckPointMap m_cpm;
	ApfsNodeMapperBTree m_omap;

	std::vector<uint8_t> m_sm_data;
	const spaceman_phys_t *m_sm;
	// Block_8_11 -> omap

	// BTree m_omap_tree; // see ApfsNodeMapperBTree
	BTree m_fq_tree_mgr;
	BTree m_fq_tree_vol;

	KeyManager m_keymgr;
};

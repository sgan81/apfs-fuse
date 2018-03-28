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
#include "ApfsNodeMapperBTree.h"
#include "KeyMgmt.h"

#include <cstdint>

class ApfsVolume;
class BlockDumper;

class ApfsContainer
{
public:
	ApfsContainer(Device &disk, uint64_t start, uint64_t len);
	~ApfsContainer();

	bool Init();

	ApfsVolume *GetVolume(int index, const std::string &passphrase);
	ApfsVolume *GetVolume(int index);
	int GetVolumeCnt() const;

	bool ReadBlocks(byte_t *data, uint64_t blkid, uint64_t blkcnt = 1) const;
	bool ReadAndVerifyHeaderBlock(byte_t *data, uint64_t blkid) const;

	uint32_t GetBlocksize() const { return m_sb.block_size; }

	bool GetVolumeKey(uint8_t *key, const apfs_uuid_t &vol_uuid);
	bool GetVolumeKey(uint8_t *key, const apfs_uuid_t &vol_uuid, const char *password);
	bool GetPasswordHint(std::string &hint, const apfs_uuid_t &vol_uuid);

	void dump(BlockDumper& bd);

private:
	Device &m_disk;
	const uint64_t m_part_start;
	const uint64_t m_part_len;
	std::string m_passphrase;
	std::string m_passphrase_kek_struct;
	std::string m_volume_kek_struct;

	APFS_Superblock_NXSB m_sb;

	APFS_Block_8_5_Spaceman m_spaceman_hdr;
	// Block_8_11 ?

	ApfsNodeMapperBTree m_nodemap_vol;

	BTree m_nidmap_bt; // 4_2/B
	BTree m_oldmgr_bt; // 8_2/9
	BTree m_oldvol_bt; // 8_2/9

	KeyManager m_keymgr;
};

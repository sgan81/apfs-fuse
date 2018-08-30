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
#include <iostream>
#include <fstream>

#include "ApfsContainer.h"
#include "ApfsVolume.h"
#include "Util.h"
#include "BlockDumper.h"
#include "Global.h"

int g_debug = 0;
bool g_lax = false;

ApfsContainer::ApfsContainer(Device &disk, uint64_t start, uint64_t len) :
	m_disk(disk),
	m_part_start(start),
	m_part_len(len),
	m_nodemap_vol(*this),
	m_nidmap_bt(*this),
	m_oldmgr_bt(*this),
	m_oldvol_bt(*this),
	m_keymgr(*this)
{
}

ApfsContainer::~ApfsContainer()
{
}

bool ApfsContainer::Init()
{
	std::vector<byte_t> blk;

	blk.resize(0x1000);

	if (!m_disk.Read(blk.data(), m_part_start, 0x1000))
		return false;

	memcpy(&m_sb, blk.data(), sizeof(APFS_NX_Superblock));

	if (m_sb.nx_magic != 0x4253584E)
		return false;

	if (m_sb.nx_block_size != 0x1000)
	{
		blk.resize(m_sb.nx_block_size);
		m_disk.Read(blk.data(), m_part_start, blk.size());
	}

	if (!VerifyBlock(blk.data(), blk.size()))
		return false;

	memcpy(&m_sb, blk.data(), sizeof(APFS_NX_Superblock));

#if 1 // Scan container for most recent superblock (might fix segfaults)
	uint64_t max_xid = 0;
	uint64_t max_bid = 0;
	uint64_t bid;
	std::vector<byte_t> tmp;

	tmp.resize(m_sb.nx_block_size);

	for (bid = m_sb.nx_xp_desc_base; bid < (m_sb.nx_xp_desc_base + m_sb.nx_xp_desc_blocks); bid++)
	{
		m_disk.Read(tmp.data(), m_part_start + bid * m_sb.nx_block_size, m_sb.nx_block_size);
		if (!VerifyBlock(tmp.data(), tmp.size()))
			continue;

		const APFS_NX_Superblock *sb = reinterpret_cast<const APFS_NX_Superblock *>(tmp.data());
		if (APFS_OBJ_TYPE(sb->hdr.o_type) != BlockType_NXSB)
			continue;

		if (sb->hdr.o_xid > max_xid)
		{
			max_xid = sb->hdr.o_xid;
			max_bid = bid;
		}
	}

	if (max_xid > m_sb.hdr.o_xid)
	{
		if (g_debug & Dbg_Errors)
			std::cout << "Found more recent xid " << max_xid << " than superblock 0 contained (" << m_sb.hdr.o_xid << ")." << std::endl;

		m_disk.Read(tmp.data(), m_part_start + max_bid * m_sb.nx_block_size, m_sb.nx_block_size);
		memcpy(&m_sb, tmp.data(), sizeof(APFS_NX_Superblock));
	}
#endif

	if (!m_nodemap_vol.Init(m_sb.nx_omap_oid, m_sb.hdr.o_xid))
		return false;

	if ((m_sb.nx_keybag_base != 0) && (m_sb.nx_keybag_blocks != 0))
	{
		if (!m_keymgr.Init(m_sb.nx_keybag_base, m_sb.nx_keybag_blocks, m_sb.nx_uuid))
		{
			std::cerr << "Initialization of KeyManager failed." << std::endl;
			return false;
		}
	}

	return true;
}

ApfsVolume *ApfsContainer::GetVolume(int index, const std::string &passphrase)
{
	ApfsVolume *vol = nullptr;
	uint64_t nodeid;
	node_info_t ni;
	bool rc;

	if (index >= 100)
		return nullptr;

	m_passphrase = passphrase;

	nodeid = m_sb.nx_fs_oid[index];

	if (nodeid == 0)
		return nullptr;

	if (!m_nodemap_vol.GetBlockID(ni, nodeid, m_sb.hdr.o_xid))
		return nullptr;

	// std::cout << std::hex << "Loading Volume " << index << ", nodeid = " << nodeid << ", version = " << m_sb.hdr.version << ", blkid = " << blkid << std::endl;

	if (ni.bid == 0)
		return nullptr;

	vol = new ApfsVolume(*this);
	rc = vol->Init(ni.bid);

	if (rc == false)
	{
		delete vol;
		vol = nullptr;
	}

	return vol;
}

int ApfsContainer::GetVolumeCnt() const
{
	int k;

	for (k = 0; k < 100; k++)
	{
		if (m_sb.nx_fs_oid[k] == 0)
			break;
	}

	return k;
}

bool ApfsContainer::ReadBlocks(byte_t * data, uint64_t blkid, uint64_t blkcnt) const
{
	uint64_t offs;
	uint64_t size;

	if ((blkid + blkcnt) > m_sb.nx_block_count)
		return false;

	offs = m_sb.nx_block_size * blkid + m_part_start;
	size = m_sb.nx_block_size * blkcnt;

	return m_disk.Read(data, offs, size);
}

bool ApfsContainer::ReadAndVerifyHeaderBlock(byte_t * data, uint64_t blkid) const
{
	if (!ReadBlocks(data, blkid))
		return false;

	if (!VerifyBlock(data, m_sb.nx_block_size))
		return false;

	return true;
}

bool ApfsContainer::GetVolumeKey(uint8_t *key, const apfs_uuid_t & vol_uuid, const char *password)
{
	if (!m_keymgr.IsValid())
		return false;

	if (password)
	{
		return m_keymgr.GetVolumeKey(key, vol_uuid, password);
	}
	else
	{
		if (m_passphrase.empty())
			return false;

		return m_keymgr.GetVolumeKey(key, vol_uuid, m_passphrase.c_str());
	}
}

bool ApfsContainer::GetPasswordHint(std::string & hint, const apfs_uuid_t & vol_uuid)
{
	return m_keymgr.GetPasswordHint(hint, vol_uuid);
}

void ApfsContainer::dump(BlockDumper& bd)
{
	std::vector<byte_t> blk;
	uint64_t blkid;

	blk.resize(GetBlocksize());
	ReadAndVerifyHeaderBlock(blk.data(), 0);

	bd.DumpNode(blk.data(), 0);

#if 1
	if (m_keymgr.IsValid())
		m_keymgr.dump(bd.st());
#endif

	/*
	if (m_keybag.size())
		bd.DumpNode(m_keybag.data(), m_sb.keybag_blk_start);
		*/

#if 0
	for (blkid = m_sb.blockid_sb_area_start; blkid < (m_sb.blockid_sb_area_start + m_sb.sb_area_cnt); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}

	for (blkid = m_sb.blockid_spaceman_area_start; blkid < (m_sb.blockid_spaceman_area_start + m_sb.spaceman_area_cnt); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}
#endif

#if 1
	for (blkid = m_sb.nx_xp_desc_base + m_sb.nx_xp_desc_index; blkid < (m_sb.nx_xp_desc_base + m_sb.nx_xp_desc_index + m_sb.nx_xp_desc_len); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}

	for (blkid = m_sb.nx_xp_data_base + m_sb.nx_xp_data_index; blkid < (m_sb.nx_xp_data_base + m_sb.nx_xp_data_index + m_sb.nx_xp_data_len); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}
#endif

	m_nodemap_vol.dump(bd);
	m_nidmap_bt.dump(bd);
	m_oldmgr_bt.dump(bd);
	m_oldvol_bt.dump(bd);
}

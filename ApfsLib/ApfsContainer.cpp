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

#include "ApfsContainer.h"
#include "ApfsVolume.h"
#include "Util.h"
#include "BlockDumper.h"
#include "Global.h"

int g_debug = 0;

ApfsContainer::ApfsContainer(Device &disk, uint64_t start, uint64_t len) :
	m_disk(disk),
	m_part_start(start),
	m_part_len(len),
	m_nodemap_vol(*this),
	m_nidmap_bt(*this),
	m_oldmgr_bt(*this),
	m_oldvol_bt(*this)
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

	memcpy(&m_sb, blk.data(), sizeof(APFS_Superblock_NXSB));

	if (m_sb.signature != 0x4253584E)
		return false;

	if (m_sb.block_size != 0x1000)
	{
		blk.resize(m_sb.block_size);
		m_disk.Read(blk.data(), m_part_start, blk.size());
	}

	if (!VerifyBlock(blk.data(), blk.size()))
		return false;

	memcpy(&m_sb, blk.data(), sizeof(APFS_Superblock_NXSB));

	m_nodemap_vol.Init(m_sb.blockid_volhdr, m_sb.hdr.version);

	return true;
}

ApfsVolume * ApfsContainer::GetVolume(int index)
{
	ApfsVolume *vol = nullptr;
	uint64_t nodeid;
	uint64_t blkid;

	if (index >= 100)
		return nullptr;

	nodeid = m_sb.nodeid_apsb[index];

	if (nodeid == 0)
		return nullptr;

	blkid = m_nodemap_vol.GetBlockID(nodeid, m_sb.hdr.version);

	// std::cout << std::hex << "Loading Volume " << index << ", nodeid = " << nodeid << ", version = " << m_sb.hdr.version << ", blkid = " << blkid << std::endl;

	if (blkid == 0)
		return nullptr;

	vol = new ApfsVolume(*this);
	vol->Init(blkid);

	return vol;
}

int ApfsContainer::GetVolumeCnt() const
{
	int k;

	for (k = 0; k < 100; k++)
	{
		if (m_sb.nodeid_apsb[k] == 0)
			break;
	}

	return k;
}

bool ApfsContainer::ReadBlocks(byte_t * data, uint64_t blkid, uint64_t blkcnt) const
{
	uint64_t offs;
	uint64_t size;

	if ((blkid + blkcnt) > m_sb.block_count)
		return false;

	offs = m_sb.block_size * blkid + m_part_start;
	size = m_sb.block_size * blkcnt;

	return m_disk.Read(data, offs, size);
}

bool ApfsContainer::ReadAndVerifyHeaderBlock(byte_t * data, uint64_t blkid) const
{
	if (!ReadBlocks(data, blkid))
		return false;

	if (!VerifyBlock(data, m_sb.block_size))
		return false;

	return true;
}

void ApfsContainer::dump(BlockDumper& bd)
{
	std::vector<byte_t> blk;
	uint64_t blkid;

	blk.resize(GetBlocksize());
	ReadAndVerifyHeaderBlock(blk.data(), 0);

	bd.DumpNode(blk.data(), 0);

	for (blkid = m_sb.blockid_sb_area_start; blkid < (m_sb.blockid_sb_area_start + m_sb.sb_area_cnt); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}

#if 0
	for (blkid = m_sb.blockid_spaceman_area_start; blkid < (m_sb.blockid_spaceman_area_start + m_sb.spaceman_area_cnt); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}
#endif
	m_nodemap_vol.dump(bd);

	// m_nidmap_bt.dump(bd);
	// m_oldmgr_bt.dump(bd);
	// m_oldvol_bt.dump(bd);
}

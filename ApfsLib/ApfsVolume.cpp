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
#include <vector>

#include "Global.h"

#include "ApfsContainer.h"
#include "ApfsVolume.h"
#include "BlockDumper.h"

ApfsVolume::ApfsVolume(ApfsContainer &container) :
	m_container(container),
	m_nodemap_dir(container),
	m_bt_directory(container),
	m_bt_blockmap(container),
	m_bt_snapshots(container)
{
	m_blockid_sb = 0;
}

ApfsVolume::~ApfsVolume()
{
}

bool ApfsVolume::Init(uint64_t blkid_volhdr)
{
	std::vector<byte_t> blk;

	m_blockid_sb = blkid_volhdr;

	blk.resize(m_container.GetBlocksize());

	if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), blkid_volhdr))
		return false;

	memcpy(&m_sb, blk.data(), sizeof(m_sb));

	if (m_sb.signature != 0x42535041)
		return false;

	m_nodemap_dir.Init(m_sb.blockid_nodemap, m_sb.hdr.version);
	m_bt_directory.Init(m_sb.nodeid_rootdir, m_sb.hdr.version, &m_nodemap_dir);
	m_bt_blockmap.Init(m_sb.blockid_blockmap, m_sb.hdr.version);
	m_bt_snapshots.Init(m_sb.blockid_4xBx10_map, m_sb.hdr.version);

	return true;
}

void ApfsVolume::dump(BlockDumper& bd)
{
	std::vector<byte_t> blk;

	blk.resize(m_container.GetBlocksize());

	if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), m_blockid_sb))
		return;

	bd.SetTextFlags(m_sb.unk_38 & 0xFF);

	bd.DumpNode(blk.data(), m_blockid_sb);

	// m_nodemap_dir.dump(bd);
	m_bt_directory.dump(bd);
	// m_bt_blockmap.dump(bd);
	// m_bt_snapshots.dump(bd);
}

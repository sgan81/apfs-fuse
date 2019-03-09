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
#include <iostream>

#include "Global.h"

#include "ApfsContainer.h"
#include "ApfsVolume.h"
#include "BlockDumper.h"
#include "Util.h"

ApfsVolume::ApfsVolume(ApfsContainer &container) :
	m_container(container),
	m_omap(container),
	m_root_tree(container, this),
	m_extentref_tree(container, this),
	m_snap_meta_tree(container, this)
{
	m_apsb_paddr = 0;
	m_is_encrypted = false;
}

ApfsVolume::~ApfsVolume()
{
}

bool ApfsVolume::Init(paddr_t apsb_paddr)
{
	std::vector<uint8_t> blk;

	m_apsb_paddr = apsb_paddr;

	blk.resize(m_container.GetBlocksize());

	if (!ReadBlocks(blk.data(), apsb_paddr, 1, 0))
		return false;

	if (!VerifyBlock(blk.data(), blk.size()))
		return false;

	memcpy(&m_sb, blk.data(), sizeof(m_sb));

	if (m_sb.apfs_magic != APFS_MAGIC)
		return false;

	if (!m_omap.Init(m_sb.apfs_omap_oid, m_sb.apfs_o.o_xid)) {
		std::cerr << "WARNING: Volume omap tree init failed." << std::endl;
		return false;
	}

	if ((m_sb.apfs_fs_flags & 3) != APFS_FS_UNENCRYPTED)
	{
		uint8_t vek[0x20];
		std::string str;

		std::cout << "Volume " << m_sb.apfs_volname << " is encrypted." << std::endl;

		if (!m_container.GetVolumeKey(vek, m_sb.apfs_vol_uuid))
		{
			if (m_container.GetPasswordHint(str, m_sb.apfs_vol_uuid))
				std::cout << "Hint: " << str << std::endl;

			std::cout << "Enter Password: ";
			GetPassword(str);

			if (!m_container.GetVolumeKey(vek, m_sb.apfs_vol_uuid, str.c_str()))
			{
				std::cout << "Wrong password!" << std::endl;
				return false;
			}
		}

		m_aes.SetKey(vek, vek + 0x10);
		m_is_encrypted = true;
	}

	if (!m_root_tree.Init(m_sb.apfs_root_tree_oid, m_sb.apfs_o.o_xid, &m_omap))
		std::cerr << "WARNING: root tree init failed" << std::endl;

	if (!m_extentref_tree.Init(m_sb.apfs_extentref_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: extentref tree init failed" << std::endl;

	if (!m_snap_meta_tree.Init(m_sb.apfs_snap_meta_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: snap meta tree init failed" << std::endl;

	return true;
}

void ApfsVolume::dump(BlockDumper& bd)
{
	std::vector<uint8_t> blk;

	blk.resize(m_container.GetBlocksize());

	if (!ReadBlocks(blk.data(), m_apsb_paddr, 1, 0))
		return;

	if (!VerifyBlock(blk.data(), blk.size()))
		return;

	bd.SetTextFlags(m_sb.apfs_incompatible_features & 0xFF);

	bd.DumpNode(blk.data(), m_apsb_paddr);

	m_omap.dump(bd);
	m_root_tree.dump(bd);
	m_extentref_tree.dump(bd);
	m_snap_meta_tree.dump(bd);
}

bool ApfsVolume::ReadBlocks(uint8_t * data, paddr_t paddr, uint64_t blkcnt, uint64_t xts_tweak)
{
	constexpr int encryption_block_size = 0x200;

	if (!m_container.ReadBlocks(data, paddr, blkcnt))
		return false;

	if (!m_is_encrypted || (xts_tweak == 0))
		return true;

	uint64_t cs_factor = m_container.GetBlocksize() / encryption_block_size;
	uint64_t uno = xts_tweak * cs_factor;
	size_t size = blkcnt * m_container.GetBlocksize();
	size_t k;

	for (k = 0; k < size; k += encryption_block_size)
	{
		m_aes.Decrypt(data + k, data + k, encryption_block_size, uno);
		uno++;
	}

	return true;
}

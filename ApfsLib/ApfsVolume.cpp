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
	m_nodemap_dir(container),
	m_bt_directory(container, this),
	m_bt_blockmap(container, this),
	m_bt_snapshots(container, this)
{
	m_blockid_sb = 0;
	m_is_encrypted = false;
}

ApfsVolume::~ApfsVolume()
{
}

bool ApfsVolume::Init(uint64_t blkid_volhdr)
{
	std::vector<byte_t> blk;

	m_blockid_sb = blkid_volhdr;

	blk.resize(m_container.GetBlocksize());

	if (!ReadBlocks(blk.data(), blkid_volhdr, 1, false, 0)) // crypto_id is unused
		return false;

	if (!VerifyBlock(blk.data(), blk.size()))
		return false;

	memcpy(&m_sb, blk.data(), sizeof(m_sb));

	if (m_sb.apfs_magic != 0x42535041)
		return false;

	if (!m_nodemap_dir.Init(m_sb.apfs_omap_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: Volume node id mapper btree init failed." << std::endl;

	if ((m_sb.apfs_fs_flags & 3) != 1)
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

	if (!m_bt_directory.Init(m_sb.apfs_root_tree_oid, m_sb.apfs_o.o_xid, &m_nodemap_dir))
		std::cerr << "WARNING: Directory btree init failed" << std::endl;

	if (!m_bt_blockmap.Init(m_sb.apfs_extentref_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: Block map btree init failed" << std::endl;

	if (!m_bt_snapshots.Init(m_sb.apfs_snap_meta_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: Snapshots btree init failed" << std::endl;

	return true;
}

void ApfsVolume::dump(BlockDumper& bd)
{
	std::vector<byte_t> blk;

	blk.resize(m_container.GetBlocksize());

	if (!ReadBlocks(blk.data(), m_blockid_sb, 1, false, 0))
		return;

	if (!VerifyBlock(blk.data(), blk.size()))
		return;

	bd.SetTextFlags(m_sb.apfs_incompatible_features & 0xFF);

	bd.DumpNode(blk.data(), m_blockid_sb);

	m_nodemap_dir.dump(bd);
	m_bt_directory.dump(bd);
	m_bt_blockmap.dump(bd);
	m_bt_snapshots.dump(bd);
}

bool ApfsVolume::ReadBlocks(byte_t * data, uint64_t blkid, uint64_t blkcnt, bool decrypt, uint64_t xts_blkid)
{
	if (!m_container.ReadBlocks(data, blkid, blkcnt))
		return false;

	if (!decrypt || !m_is_encrypted)
		return true;

	uint64_t cs_factor = m_container.GetBlocksize() / 0x200;
	uint64_t uno = xts_blkid * cs_factor;
	size_t size = blkcnt * m_container.GetBlocksize();
	size_t k;

	for (k = 0; k < size; k += 0x200)
	{
		m_aes.Decrypt(data + k, data + k, 0x200, uno);
		uno++;
	}

	return true;
}

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
	m_cpm(*this),
	m_omap(*this),
	// m_omap_tree(*this),
	m_fq_tree_mgr(*this),
	m_fq_tree_vol(*this),
	m_keymgr(*this)
{
	m_sm = nullptr;
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

	memcpy(&m_nx, blk.data(), sizeof(nx_superblock_t));

	if (m_nx.nx_magic != NX_MAGIC)
		return false;

	if (m_nx.nx_block_size != 0x1000)
	{
		blk.resize(m_nx.nx_block_size);
		m_disk.Read(blk.data(), m_part_start, blk.size());
	}

	if (!VerifyBlock(blk.data(), blk.size()))
		return false;

	memcpy(&m_nx, blk.data(), sizeof(nx_superblock_t));

#if 1 // Scan container for most recent superblock (might fix segfaults)
	uint64_t max_xid = 0;
	uint64_t max_bid = 0;
	paddr_t bid;
	std::vector<byte_t> tmp;

	tmp.resize(m_nx.nx_block_size);

	for (bid = m_nx.nx_xp_desc_base; bid < (m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_blocks); bid++)
	{
		m_disk.Read(tmp.data(), m_part_start + bid * m_nx.nx_block_size, m_nx.nx_block_size);
		if (!VerifyBlock(tmp.data(), tmp.size()))
			continue;

		const nx_superblock_t *sb = reinterpret_cast<const nx_superblock_t *>(tmp.data());
		if ((sb->nx_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_NX_SUPERBLOCK)
			continue;

		if (sb->nx_o.o_xid > max_xid)
		{
			max_xid = sb->nx_o.o_xid;
			max_bid = bid;
		}
	}

	if (max_xid > m_nx.nx_o.o_xid)
	{
		if (g_debug & Dbg_Errors)
			std::cout << "Found more recent xid " << max_xid << " than superblock 0 contained (" << m_nx.nx_o.o_xid << ")." << std::endl;

		m_disk.Read(tmp.data(), m_part_start + max_bid * m_nx.nx_block_size, m_nx.nx_block_size);
		memcpy(&m_nx, tmp.data(), sizeof(nx_superblock_t));
	}
#endif

	if (!m_cpm.Init(m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_index))
	{
		std::cerr << "Failed to load checkpoint map" << std::endl;
		return false;
	}

	if (!m_omap.Init(m_nx.nx_omap_oid, m_nx.nx_o.o_xid))
	{
		std::cerr << "Failed to load nx omap" << std::endl;
		return false;
	}

	node_info_t ni;
	if (!m_cpm.GetBlockID(ni, m_nx.nx_spaceman_oid, m_nx.nx_o.o_xid))
	{
		std::cerr << "Failed to map spaceman oid" << std::endl;
		return false;
	}

	m_sm_data.resize(GetBlocksize());
	ReadBlocks(m_sm_data.data(), ni.bid, 1);
	m_sm = reinterpret_cast<const spaceman_phys_t *>(m_sm_data.data());

	if ((m_sm->sm_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_SPACEMAN)
	{
		std::cerr << "Spaceman has wrong type " << m_sm->sm_o.o_type << std::endl;
		return false;
	}

	if (m_sm->sm_fq[SFQ_IP].sfq_tree_oid != 0)
		m_fq_tree_mgr.Init(m_sm->sm_fq[SFQ_IP].sfq_tree_oid, m_sm->sm_o.o_xid, &m_cpm);

	if (m_sm->sm_fq[SFQ_MAIN].sfq_tree_oid != 0)
		m_fq_tree_vol.Init(m_sm->sm_fq[SFQ_MAIN].sfq_tree_oid, m_sm->sm_o.o_xid, &m_cpm);

	// m_omap_tree.Init(m_nx.nx_omap_oid, m_nx.hdr.o_xid, nullptr);

	// m_sb.nx_spaceman_oid

	if ((m_nx.nx_keylocker.pr_start_addr != 0) && (m_nx.nx_keylocker.pr_block_count != 0))
	{
		if (!m_keymgr.Init(m_nx.nx_keylocker.pr_start_addr, m_nx.nx_keylocker.pr_block_count, m_nx.nx_uuid))
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

	nodeid = m_nx.nx_fs_oid[index];

	if (nodeid == 0)
		return nullptr;

	if (!m_omap.GetBlockID(ni, nodeid, m_nx.nx_o.o_xid))
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
		if (m_nx.nx_fs_oid[k] == 0)
			break;
	}

	return k;
}

bool ApfsContainer::ReadBlocks(byte_t * data, uint64_t blkid, uint64_t blkcnt) const
{
	uint64_t offs;
	uint64_t size;

	if ((blkid + blkcnt) > m_nx.nx_block_count)
		return false;

	offs = m_nx.nx_block_size * blkid + m_part_start;
	size = m_nx.nx_block_size * blkcnt;

	return m_disk.Read(data, offs, size);
}

bool ApfsContainer::ReadAndVerifyHeaderBlock(byte_t * data, uint64_t blkid) const
{
	if (!ReadBlocks(data, blkid))
		return false;

	if (!VerifyBlock(data, m_nx.nx_block_size))
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

	bd.st() << "Dumping Container" << std::endl;
	bd.st() << "-----------------" << std::endl;
	bd.st() << std::endl;
	bd.st() << std::endl;

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
	for (blkid = m_sb.nx_xp_desc_base; blkid < (m_sb.nx_xp_desc_base + m_sb.nx_xp_desc_blocks); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}

	for (blkid = m_sb.nx_xp_data_base; blkid < (m_sb.nx_xp_data_base + m_sb.nx_xp_data_blocks); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}
#endif

#if 0
	for (blkid = m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_index; blkid < (m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_index + m_nx.nx_xp_desc_len); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}

	for (blkid = m_nx.nx_xp_data_base + m_nx.nx_xp_data_index; blkid < (m_nx.nx_xp_data_base + m_nx.nx_xp_data_index + m_nx.nx_xp_data_len); blkid++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), blkid);
		bd.DumpNode(blk.data(), blkid);
	}
#endif

	if (m_nx.nx_efi_jumpstart)
	{
		ReadAndVerifyHeaderBlock(blk.data(), m_nx.nx_efi_jumpstart);
		bd.DumpNode(blk.data(), m_nx.nx_efi_jumpstart);
	}

	ReadAndVerifyHeaderBlock(blk.data(), m_nx.nx_omap_oid);
	bd.DumpNode(blk.data(), m_nx.nx_omap_oid);

	bd.DumpNode(m_sm_data.data(), m_nx.nx_spaceman_oid);

	uint64_t oid;
	size_t k;

	for (size_t k = 0; k < m_sm->sm_ip_bm_block_count; k++)
	{
		oid = m_sm->sm_ip_bm_base + k;

		bd.st() << "Dumping IP Bitmap block " << k << std::endl;

		ReadBlocks(blk.data(), oid);
		bd.DumpNode(blk.data(), oid);

		bd.st() << std::endl;
	}

	m_omap.dump(bd);
	// m_omap_tree.dump(bd);
	m_fq_tree_mgr.dump(bd);
	m_fq_tree_vol.dump(bd);

	const le<uint64_t> *cxb_oid = reinterpret_cast<const le<uint64_t> *>(m_sm_data.data() + m_sm->sm_dev[SD_MAIN].sm_addr_offset);
	uint32_t cib_cnt = m_sm->sm_dev[SD_MAIN].sm_cib_count;
	uint32_t cab_cnt = m_sm->sm_dev[SD_MAIN].sm_cab_count;

	uint32_t cib_id;
	uint32_t cab_id;

	std::vector<uint64_t> cib_oid_list;
	std::vector<uint8_t> cib_data(GetBlocksize());

	cib_oid_list.reserve(cib_cnt);

	if (cab_cnt != 0)
	{
		for (cab_id = 0; cab_id < cab_cnt; cab_id++)
		{
			ReadAndVerifyHeaderBlock(blk.data(), cxb_oid[cab_id]);
			bd.DumpNode(blk.data(), cxb_oid[cab_id]);

			const cib_addr_block_t *cab = reinterpret_cast<cib_addr_block_t *>(blk.data());

			for (cib_id = 0; cib_id < cab->cab_cib_count; cib_id++)
				cib_oid_list.push_back(cab->cab_cib_addr[cib_id]);
		}
	}
	else
	{
		for (cib_id = 0; cib_id < cib_cnt; cib_id++)
			cib_oid_list.push_back(cxb_oid[cib_id]);
	}

	for (cib_id = 0; cib_id < cib_cnt; cib_id++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), cib_oid_list[cib_id]);
		bd.DumpNode(blk.data(), cib_oid_list[cib_id]);
	}
}

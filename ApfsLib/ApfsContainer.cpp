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

ApfsContainer::ApfsContainer(Device *disk_main, uint64_t main_start, uint64_t main_len, Device *disk_tier2, uint64_t tier2_start, uint64_t tier2_len) :
	m_main_disk(disk_main),
	m_main_part_start(main_start),
	m_main_part_len(main_len),
	m_tier2_disk(disk_tier2),
	m_tier2_part_start(tier2_start),
	m_tier2_part_len(tier2_len),
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

bool ApfsContainer::Init(xid_t req_xid)
{
	std::vector<uint8_t> blk;

	blk.resize(0x1000);

	if (!m_main_disk->Read(blk.data(), m_main_part_start, 0x1000))
	{
		std::cerr << "Reading block 0 from main device failed." << std::endl;
		return false;
	}

	memcpy(&m_nx, blk.data(), sizeof(nx_superblock_t));

	if (m_nx.nx_magic != NX_MAGIC)
	{
		std::cerr << "This doesn't seem to be an apfs volume (invalid superblock)." << std::endl;
		return false;
	}

	if (m_nx.nx_block_size != 0x1000)
	{
		blk.resize(m_nx.nx_block_size);
		m_main_disk->Read(blk.data(), m_main_part_start, blk.size());
	}

	if (!VerifyBlock(blk.data(), blk.size()))
		return false;

	memcpy(&m_nx, blk.data(), sizeof(nx_superblock_t));

	// Scan container for most recent superblock (might fix segfaults)
	uint64_t max_xid = 0;
	paddr_t max_paddr = 0;
	paddr_t paddr;
	std::vector<uint8_t> tmp;

	tmp.resize(m_nx.nx_block_size);

	for (paddr = m_nx.nx_xp_desc_base; paddr < (m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_blocks); paddr++)
	{
		if (!ReadBlocks(tmp.data(), paddr, 1))
			return false;

		if (!VerifyBlock(tmp.data(), tmp.size()))
			continue;

		const nx_superblock_t *sb = reinterpret_cast<const nx_superblock_t *>(tmp.data());
		if ((sb->nx_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_NX_SUPERBLOCK)
			continue;

		if (req_xid) {
			if (req_xid == sb->nx_o.o_xid) {
				max_xid = req_xid;
				max_paddr = paddr;
				break;
			}
		} else {
			if (sb->nx_o.o_xid > max_xid)
			{
				max_xid = sb->nx_o.o_xid;
				max_paddr = paddr;
			}
		}
	}

	if (max_paddr)
	{
		// if (g_debug & Dbg_Errors)
		//	std::cout << "Found more recent xid " << max_xid << " than superblock 0 contained (" << m_nx.nx_o.o_xid << ")." << std::endl;
		if (g_debug & Dbg_Info)
			std::cout << "Mounting xid different from NXSB at 0 (xid = " << m_nx.nx_o.o_xid << "). xid = " << max_xid << std::endl;

		ReadBlocks(tmp.data(), max_paddr, 1);

		memcpy(&m_nx, tmp.data(), sizeof(nx_superblock_t));
	}

	if (g_debug & Dbg_Info)
		std::cout << "Mounting xid " << m_nx.nx_o.o_xid << std::endl;

	if ((m_nx.nx_incompatible_features & NX_INCOMPAT_FUSION) && !m_tier2_disk)
	{
		std::cerr << "Need to specify two devices for a fusion drive." << std::endl;
		return false;
	}

	if (!m_cpm.Init(m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_index, m_nx.nx_xp_desc_len - 1))
	{
		std::cerr << "Failed to load checkpoint map" << std::endl;
		return false;
	}

	if (!m_omap.Init(m_nx.nx_omap_oid, m_nx.nx_o.o_xid))
	{
		std::cerr << "Failed to load nx omap" << std::endl;
		return false;
	}

	omap_res_t omr;
	if (!m_cpm.Lookup(omr, m_nx.nx_spaceman_oid, m_nx.nx_o.o_xid))
	{
		std::cerr << "Failed to map spaceman oid" << std::endl;
		return false;
	}

	m_sm_data.resize(omr.size);
	ReadBlocks(m_sm_data.data(), omr.paddr, omr.size / GetBlocksize());
	m_sm = reinterpret_cast<const spaceman_phys_t *>(m_sm_data.data());

	if (!VerifyBlock(m_sm_data.data(), m_sm_data.size())) {
		std::cerr << "Checksum error in spaceman" << std::endl;
		return false;
	}

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

ApfsVolume *ApfsContainer::GetVolume(unsigned int fsid, const std::string &passphrase, xid_t snap_xid)
{
	ApfsVolume *vol = nullptr;
	oid_t oid;
	omap_res_t omr;
	bool rc;

	if (fsid >= 100)
		return nullptr;

	m_passphrase = passphrase;

	oid = m_nx.nx_fs_oid[fsid];

	if (oid == 0)
		return nullptr;

	if (!m_omap.Lookup(omr, oid, m_nx.nx_o.o_xid))
		return nullptr;

	// std::cout << std::hex << "Loading Volume " << index << ", nodeid = " << nodeid << ", version = " << m_sb.hdr.version << ", blkid = " << blkid << std::endl;

	if (omr.paddr == 0)
		return nullptr;

	vol = new ApfsVolume(*this);
	if (snap_xid != 0)
		rc = vol->MountSnapshot(omr.paddr, snap_xid);
	else
		rc = vol->Init(omr.paddr);

	if (rc == false)
	{
		delete vol;
		vol = nullptr;
	}

	return vol;
}

bool ApfsContainer::GetVolumeInfo(unsigned int fsid, apfs_superblock_t& apsb)
{
	oid_t oid;
	omap_res_t omr;
	std::vector<uint8_t> apsb_raw;

	if (fsid >= NX_MAX_FILE_SYSTEMS)
		return false;

	oid = m_nx.nx_fs_oid[fsid];

	if (oid == 0)
		return false;

	if (!m_omap.Lookup(omr, oid, m_nx.nx_o.o_xid))
		return false;

	if (omr.paddr == 0)
		return false;

	apsb_raw.resize(GetBlocksize());

	if (!ReadAndVerifyHeaderBlock(apsb_raw.data(), omr.paddr))
		return false;

	memcpy(&apsb, apsb_raw.data(), sizeof(apfs_superblock_t));

	return true;
}


bool ApfsContainer::ReadBlocks(uint8_t * data, paddr_t paddr, uint64_t blkcnt) const
{
	uint64_t offs;
	uint64_t size;

	//if ((paddr + blkcnt) > m_nx.nx_block_count)
	//	return false;

	offs = m_nx.nx_block_size * paddr;
	size = m_nx.nx_block_size * blkcnt;

	if (offs & FUSION_TIER2_DEVICE_BYTE_ADDR)
	{
		if (!m_tier2_disk)
			return false;

		offs = offs - FUSION_TIER2_DEVICE_BYTE_ADDR + m_tier2_part_start;
		return m_tier2_disk->Read(data, offs, size);
	}
	else
	{
		if (!m_main_disk)
			return false;

		offs = offs + m_main_part_start;
		return m_main_disk->Read(data, offs, size);
	}
}

bool ApfsContainer::ReadAndVerifyHeaderBlock(uint8_t * data, paddr_t paddr) const
{
	if (!ReadBlocks(data, paddr))
		return false;

	if (!VerifyBlock(data, m_nx.nx_block_size)) {
		if (g_debug & Dbg_Errors) {
			std::cerr << "ReadAndVerifyHeaderBlock checksum error." << std::endl;
			DumpHex(std::cerr, data, m_nx.nx_block_size);
		}
		return false;
	}

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
	std::vector<uint8_t> blk;
	paddr_t paddr;
	uint32_t index;
	uint32_t last_index;

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
	for (paddr = m_nx.nx_xp_desc_base; paddr < (m_nx.nx_xp_desc_base + m_nx.nx_xp_desc_blocks); paddr++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr);
		bd.DumpNode(blk.data(), paddr);
	}
#endif
#if 0
	for (paddr = m_nx.nx_xp_data_base; paddr < (m_nx.nx_xp_data_base + m_nx.nx_xp_data_blocks); paddr++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr);
		bd.DumpNode(blk.data(), paddr);
	}
#endif

#if 1
	bd.st() << std::endl << "Dumping XP desc area (current SB):" << std::endl;
	paddr = m_nx.nx_xp_desc_base;
	last_index = m_nx.nx_xp_desc_index + m_nx.nx_xp_desc_len;
	if (last_index >= m_nx.nx_xp_desc_blocks)
		last_index -= m_nx.nx_xp_desc_blocks;

	for (index = m_nx.nx_xp_desc_index; index != last_index;)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr + index);
		bd.DumpNode(blk.data(), paddr + index);
		index++;
		if (index >= m_nx.nx_xp_desc_blocks)
			index -= m_nx.nx_xp_desc_blocks;
	}

	bd.st() << std::endl << "Dumping XP data area (current SB):" << std::endl;
	paddr = m_nx.nx_xp_data_base;
	last_index = m_nx.nx_xp_data_index + m_nx.nx_xp_data_len;
	if (last_index >= m_nx.nx_xp_data_blocks)
		last_index -= m_nx.nx_xp_data_blocks;

	for (index = m_nx.nx_xp_data_index; index != last_index;)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr + index);
		bd.DumpNode(blk.data(), paddr + index);
		index++;
		if (index >= m_nx.nx_xp_data_blocks)
			index -= m_nx.nx_xp_data_blocks;
	}
#endif

	if (m_nx.nx_efi_jumpstart)
	{
		ReadAndVerifyHeaderBlock(blk.data(), m_nx.nx_efi_jumpstart);
		bd.DumpNode(blk.data(), m_nx.nx_efi_jumpstart);
	}

	ReadAndVerifyHeaderBlock(blk.data(), m_nx.nx_omap_oid);
	bd.DumpNode(blk.data(), m_nx.nx_omap_oid);

	{
		size_t bs = bd.GetBlockSize();
		bd.SetBlockSize(m_sm_data.size());
		bd.DumpNode(m_sm_data.data(), m_nx.nx_spaceman_oid);
		bd.SetBlockSize(bs);
	}

	uint64_t oid;
	size_t k;

	for (k = 0; k < m_sm->sm_ip_bm_block_count; k++)
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

	const le_uint64_t *cxb_oid = reinterpret_cast<const le_uint64_t *>(m_sm_data.data() + m_sm->sm_dev[SD_MAIN].sm_addr_offset);
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

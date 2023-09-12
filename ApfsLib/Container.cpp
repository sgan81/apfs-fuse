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
#include <cinttypes>

#include "Container.h"
#include "Volume.h"
#include "Util.h"
#include "BlockDumper.h"
#include "Global.h"
#include "ObjCache.h"
#include "OMap.h"
#include "Spaceman.h"

int g_debug = 0;
bool g_lax = false;

Container::Container() : m_keymgr(*this)
{
	m_main_disk = nullptr;
	m_main_part_start = 0;
	m_main_part_len = 0;
	m_tier2_disk = nullptr;
	m_tier2_part_start = 0;
	m_tier2_part_len = 0;
}

Container::~Container()
{
}

int Container::init(const void* params)
{
	(void)params;
	m_nxsb = reinterpret_cast<const nx_superblock_t*>(data());
	return 0;
}

int Container::Mount(ObjPtr<Container>& ptr, Device *disk_main, uint64_t main_start, uint64_t main_len, Device *disk_tier2, uint64_t tier2_start, uint64_t tier2_len, xid_t req_xid)
{
	Container* nx_obj = nullptr;

	std::vector<uint8_t> blk;
	const nx_superblock_t* nxsb;
	int err;

	blk.resize(NX_DEFAULT_BLOCK_SIZE);

	err = disk_main->Read(blk.data(), main_start, NX_DEFAULT_BLOCK_SIZE) ? 0 : EIO; // TODO
	if (err) {
		log_error("Reading block 0 from main device failed, err = %d\n", err);
		return false;
	}

	nxsb = reinterpret_cast<const nx_superblock_t*>(blk.data());
	if (nxsb->nx_magic != NX_MAGIC) {
		log_error("This is not an apfs volume (invalid superblock).\n");
		// TODO help the user ...
		return EINVAL;
	}

	if (nxsb->nx_block_size != NX_DEFAULT_BLOCK_SIZE) {
		blk.resize(nxsb->nx_block_size);
		err = disk_main->Read(blk.data(), main_start, blk.size());
		nxsb = reinterpret_cast<const nx_superblock_t*>(blk.data());
	}

	if (!VerifyBlock(blk.data(), blk.size())) {
		log_error("nx superblock checksum error!\n");
		return EINVAL;
	}

	if ((nxsb->nx_incompatible_features & NX_INCOMPAT_FUSION) && !disk_tier2) {
		log_error("Need to specify two devices for a fusion drive.\n");
		return EINVAL;
	}

	nx_obj = new Container();

	nx_obj->m_main_disk = disk_main;
	nx_obj->m_main_part_start = main_start;
	nx_obj->m_main_part_len = main_len;
	nx_obj->m_tier2_disk = disk_tier2;
	nx_obj->m_tier2_part_start = tier2_start;
	nx_obj->m_tier2_part_len = tier2_len;
	nx_obj->setData(blk.data(), blk.size());

	// Scan container for most recent superblock (might fix segfaults)
	uint64_t max_xid = 0;
	paddr_t max_paddr = 0;
	paddr_t paddr;
	std::vector<uint8_t> tmp;

	tmp.resize(nxsb->nx_block_size);
	const nx_superblock_t *xpsb = reinterpret_cast<const nx_superblock_t *>(tmp.data());

	for (paddr = nxsb->nx_xp_desc_base; paddr < (nxsb->nx_xp_desc_base + nxsb->nx_xp_desc_blocks); paddr++) {
		err = nx_obj->ReadBlocks(tmp.data(), paddr, 1);
		if (err) {
			log_error("Read blocks failed, err = %d\n", err);
			return err;
		}

		if (!VerifyBlock(tmp.data(), tmp.size())) {
			log_warn("checksum error in xp desc area\n");
			// DumpHex(std::cout, tmp.data(), tmp.size());
			continue;
		}

		if ((xpsb->nx_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_NX_SUPERBLOCK)
			continue;

		if (req_xid) {
			if (req_xid == xpsb->nx_o.o_xid) {
				max_xid = req_xid;
				max_paddr = paddr;
				break;
			}
		} else {
			if (xpsb->nx_o.o_xid > max_xid)
			{
				max_xid = xpsb->nx_o.o_xid;
				max_paddr = paddr;
			}
		}
	}

	if (max_xid == 0) {
		log_error("Checkpoint search failed.\n");
		return EINVAL;
	}

	if (max_paddr) {
		if (nxsb->nx_o.o_xid != max_xid)
			log_warn("Mounting xid different from NXSB at 0 (xid = %" PRIx64 "). xid = %" PRIx64 "\n", nxsb->nx_o.o_xid, max_xid);

		nx_obj->ReadBlocks(tmp.data(), max_paddr, 1);
	}

	log_debug("Mounting xid %" PRIx64 "\n", max_xid);

	ObjCache* oc = new ObjCache();
	nx_obj->setData(tmp.data(), tmp.size());
	oc->setContainer(nx_obj, max_paddr);

	nx_obj->FinishMount();

	ptr = nx_obj;
	return 0;
}

int Container::FinishMount()
{
	paddr_t paddr;
	std::vector<uint8_t> cpm_data;
	const checkpoint_map_phys_t *cpm = nullptr;
	uint32_t i;
	uint32_t k;
	int err;

	cpm_data.resize(m_nxsb->nx_block_size);
	cpm = reinterpret_cast<const checkpoint_map_phys_t*>(cpm_data.data());

	for (i = m_nxsb->nx_xp_desc_index; i != m_nxsb->nx_xp_desc_next;) {
		paddr = m_nxsb->nx_xp_desc_base + i;

		err = ReadAndVerifyHeaderBlock(cpm_data.data(), paddr);
		if (err) return err;

		cpm = reinterpret_cast<const checkpoint_map_phys_t*>(cpm_data.data());

		if ((cpm->cpm_o.o_type & OBJECT_TYPE_MASK) == OBJECT_TYPE_CHECKPOINT_MAP) {
			log_debug("cpm: count=%d type=%08x\n", cpm->cpm_count, cpm->cpm_o.o_type);
			for (k = 0; k < cpm->cpm_count; k++) {
				const checkpoint_mapping_t& cm = cpm->cpm_map[k];

				log_debug("%x %x %x %x fs_oid=%" PRIx64 " oid=%" PRIx64 " paddr=%" PRIx64 "\n",
					cm.cpm_type, cm.cpm_subtype, cm.cpm_size, cm.cpm_pad, cm.cpm_fs_oid, cm.cpm_oid, cm.cpm_paddr);
				// For now, load only sm ...
				if ((cm.cpm_type & OBJECT_TYPE_MASK) == OBJECT_TYPE_SPACEMAN) {
					Object* dummy;
					err = oc().getObj(dummy, nullptr, cm.cpm_oid, 0, cm.cpm_type, cm.cpm_subtype, cm.cpm_size, cm.cpm_paddr);
					if (err)
						log_debug("Failed to load ephemeral, oid %" PRIx64 ", err %d\n", cm.cpm_oid, err);
				}
			}
		}

		i++;
		if (i >= m_nxsb->nx_xp_desc_blocks)
			i = 0;
	}

	return 0;

#if 0 // TODO


	if (!m_cpm.Init(m_nxsb->nx_xp_desc_base + m_nxsb->nx_xp_desc_index, m_nxsb->nx_xp_desc_len - 1))
	{
	std::cerr << "Failed to load checkpoint map" << std::endl;
	return false;
}

if (!m_omap.Init(m_nxsb->nx_omap_oid, m_nxsb->nx_o.o_xid))
{
std::cerr << "Failed to load nx omap" << std::endl;
return false;
}

omap_res_t omr;
if (!m_cpm.Lookup(omr, m_nxsb->nx_spaceman_oid, m_nxsb->nx_o.o_xid))
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

/*
 *	if (m_sm->sm_fq[SFQ_IP].sfq_tree_oid != 0)
 *		m_fq_tree_mgr.Init(m_sm->sm_fq[SFQ_IP].sfq_tree_oid, m_sm->sm_o.o_xid, &m_cpm);
 *
 *	if (m_sm->sm_fq[SFQ_MAIN].sfq_tree_oid != 0)
 *		m_fq_tree_vol.Init(m_sm->sm_fq[SFQ_MAIN].sfq_tree_oid, m_sm->sm_o.o_xid, &m_cpm);
 */

// m_omap_tree.Init(m_nxsb->nx_omap_oid, m_nxsb->hdr.o_xid, nullptr);

// m_sb.nx_spaceman_oid

if ((m_nxsb->nx_keylocker.pr_start_addr != 0) && (m_nxsb->nx_keylocker.pr_block_count != 0))
{
if (!m_keymgr.Init(m_nxsb->nx_keylocker.pr_start_addr, m_nxsb->nx_keylocker.pr_block_count, m_nxsb->nx_uuid))
{
std::cerr << "Initialization of KeyManager failed." << std::endl;
return false;
}
}
#endif
}

int Container::Unmount(ObjPtr<Container>& ptr)
{
	Container* nx = ptr.release();
	if (nx == nullptr) return 0;
	ObjCache* cache = &nx->oc();
	delete nx;
	delete cache;
	return 0;
}

int Container::MountVolume(ObjPtr<Volume>& vol, unsigned int fsid, const std::string &passphrase, xid_t snap_xid)
{
	oid_t oid;
	int err;

	if (fsid >= 100)
		return EINVAL;

	m_passphrase = passphrase;

	oid = m_nxsb->nx_fs_oid[fsid];

	if (oid == 0) {
		log_error("Volume %d does not exist.\n", fsid);
		return ENOENT;
	}

	err = oc().getObj(vol, nullptr, oid, 0, OBJECT_TYPE_FS, 0, 0, 0, nullptr);
	if (err)
		return err;

	if (snap_xid != 0)
		err = vol->MountSnapshot(vol->paddr(), snap_xid); // TODO paddr braucht es nicht mehr ...
	else
		err = vol->Mount();

	return err;
}

int Container::GetVolumeInfo(unsigned int fsid, apfs_superblock_t& apsb)
{
	oid_t oid;
	paddr_t paddr;
	int err;
	std::vector<uint8_t> apsb_raw;
	ObjPtr<OMap> omap;

	if (fsid >= NX_MAX_FILE_SYSTEMS)
		return false;

	oid = m_nxsb->nx_fs_oid[fsid];

	if (oid == 0)
		return false;

	err = getOMap(omap);
	if (err) return err;

	err = omap->lookup(oid, 0, nullptr, nullptr, nullptr, &paddr);
	if (err) return err;

	apsb_raw.resize(GetBlocksize());

	err = ReadAndVerifyHeaderBlock(apsb_raw.data(), paddr);
	if (err) return err;

	memcpy(&apsb, apsb_raw.data(), sizeof(apfs_superblock_t));

	return true;
}

int Container::ReadBlocks(uint8_t * data, paddr_t paddr, uint64_t blkcnt) const
{
	uint64_t offs;
	uint64_t size;
	bool ok;

	//if ((paddr + blkcnt) > m_nxsb->nx_block_count)
	//	return false;

	offs = m_nxsb->nx_block_size * paddr;
	size = m_nxsb->nx_block_size * blkcnt;

	if (offs & FUSION_TIER2_DEVICE_BYTE_ADDR)
	{
		if (!m_tier2_disk)
			return EINVAL;

		offs = offs - FUSION_TIER2_DEVICE_BYTE_ADDR + m_tier2_part_start;
		ok = m_tier2_disk->Read(data, offs, size); // TODO
		if (!ok)
			log_error("Failed to read from tier2 disk, %" PRIx64 " / %" PRIx64 "\n", paddr, blkcnt);
	}
	else
	{
		if (!m_main_disk)
			return EINVAL;

		offs = offs + m_main_part_start;
		ok = m_main_disk->Read(data, offs, size); // TODO
		if (!ok)
			log_error("Failed to read from main disk, %" PRIx64 " / %" PRIx64 "\n", paddr, blkcnt);
	}

	return ok ? 0 : EIO; // TODO
}

int Container::ReadAndVerifyHeaderBlock(uint8_t * data, paddr_t paddr) const
{
	int err;

	err = ReadBlocks(data, paddr);
	if (err) return err;

	if (!VerifyBlock(data, m_nxsb->nx_block_size)) {
		log_error("checksum error.\n");
		DumpHex(std::cerr, data, m_nxsb->nx_block_size);
		return EINVAL;
	}

	return 0;
}

int Container::getOMap(ObjPtr<OMap>& ptr)
{
	if (!m_omap) {
		int err = oc().getObj(m_omap, NULL, m_nxsb->nx_omap_oid, 0, OBJECT_TYPE_OMAP | OBJ_PHYSICAL, 0, 0, 0);
		if (err) {
			log_error("nx: failed to get omap, err = %d\n", err);
			return err;
		}
	}
	ptr = m_omap;
	return 0;
}

uint64_t Container::GetFreeBlocks()
{
	ObjPtr<Spaceman> sm;
	int err;

	err = getSpaceman(sm);
	if (err) return 0;

	return sm->getFreeBlocks();
}

int Container::GetVolumeKey(uint8_t *key, const apfs_uuid_t & vol_uuid, const char *password)
{
	int err = 0;
	bool ok;

	if (!m_keymgr.IsValid())
		err = loadKeybag();
	if (err) return err;

	if (password) {
		ok = m_keymgr.GetVolumeKey(key, vol_uuid, password);
	} else {
		if (m_passphrase.empty())
			ok = false;
		else
			ok = m_keymgr.GetVolumeKey(key, vol_uuid, m_passphrase.c_str());
	}
	return ok ? 0 : EPERM; // TODO
}

bool Container::GetPasswordHint(std::string & hint, const apfs_uuid_t & vol_uuid)
{
	return m_keymgr.GetPasswordHint(hint, vol_uuid);
}

bool Container::IsUnencrypted()
{
	int err;
	if (!m_keymgr.IsValid()) {
		err = loadKeybag();
		if (err) return false;
	}
	return m_keymgr.IsUnencrypted();
}


void Container::dump(BlockDumper& bd)
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
	for (paddr = m_nxsb->nx_xp_desc_base; paddr < (m_nxsb->nx_xp_desc_base + m_nxsb->nx_xp_desc_blocks); paddr++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr);
		bd.DumpNode(blk.data(), paddr);
	}
#endif
#if 0
	for (paddr = m_nxsb->nx_xp_data_base; paddr < (m_nxsb->nx_xp_data_base + m_nxsb->nx_xp_data_blocks); paddr++)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr);
		bd.DumpNode(blk.data(), paddr);
	}
#endif

#if 1
	bd.st() << std::endl << "Dumping XP desc area (current SB):" << std::endl;
	paddr = m_nxsb->nx_xp_desc_base;
	last_index = m_nxsb->nx_xp_desc_index + m_nxsb->nx_xp_desc_len;
	if (last_index >= m_nxsb->nx_xp_desc_blocks)
		last_index -= m_nxsb->nx_xp_desc_blocks;

	for (index = m_nxsb->nx_xp_desc_index; index != last_index;)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr + index);
		bd.DumpNode(blk.data(), paddr + index);
		index++;
		if (index >= m_nxsb->nx_xp_desc_blocks)
			index -= m_nxsb->nx_xp_desc_blocks;
	}

	bd.st() << std::endl << "Dumping XP data area (current SB):" << std::endl;
	paddr = m_nxsb->nx_xp_data_base;
	last_index = m_nxsb->nx_xp_data_index + m_nxsb->nx_xp_data_len;
	if (last_index >= m_nxsb->nx_xp_data_blocks)
		last_index -= m_nxsb->nx_xp_data_blocks;

	for (index = m_nxsb->nx_xp_data_index; index != last_index;)
	{
		ReadAndVerifyHeaderBlock(blk.data(), paddr + index);
		bd.DumpNode(blk.data(), paddr + index);
		index++;
		if (index >= m_nxsb->nx_xp_data_blocks)
			index -= m_nxsb->nx_xp_data_blocks;
	}
#endif

	if (m_nxsb->nx_efi_jumpstart)
	{
		ReadAndVerifyHeaderBlock(blk.data(), m_nxsb->nx_efi_jumpstart);
		bd.DumpNode(blk.data(), m_nxsb->nx_efi_jumpstart);
	}

	// TODO move to omap
	ReadAndVerifyHeaderBlock(blk.data(), m_nxsb->nx_omap_oid);
	bd.DumpNode(blk.data(), m_nxsb->nx_omap_oid);

	m_fq_tree_mgr.dump(bd);
	m_fq_tree_vol.dump(bd);
#if 0 // TODO move to spaceman
	{
		size_t bs = bd.GetBlockSize();
		bd.SetBlockSize(m_sm_data.size());
		bd.DumpNode(m_sm_data.data(), m_nxsb->nx_spaceman_oid);
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

	// m_omap.dump(bd); // TODO
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
#endif
}

int Container::getSpaceman(ObjPtr<Spaceman>& sm)
{
	int err = 0;
	if (!m_sm) {
		err = oc().getObj(m_sm, nullptr, m_nxsb->nx_spaceman_oid, 0, OBJECT_TYPE_SPACEMAN | OBJ_EPHEMERAL, 0, 0, 0, nullptr);
		if (err) return err;
	}
	sm = m_sm;
	return 0;
}

int Container::loadKeybag()
{
	bool ok;
	if (m_nxsb->nx_keylocker.pr_start_addr == 0 || m_nxsb->nx_keylocker.pr_block_count == 0) {
		log_error("No keylocker location specified.\n");
		return EINVAL;
	}
	// TODO
	ok = m_keymgr.Init(m_nxsb->nx_keylocker.pr_start_addr, m_nxsb->nx_keylocker.pr_block_count, m_nxsb->nx_uuid);
	if (!ok) {
		log_error("Keybag initialization failed.\n");
		return EINVAL;
	}
	return 0;
}

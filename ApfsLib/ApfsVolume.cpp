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
	m_fs_tree(container, this),
	m_extentref_tree(container, this),
	m_snap_meta_tree(container, this),
	m_fext_tree(container, this)
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

	if (!m_fs_tree.Init(m_sb.apfs_root_tree_oid, m_sb.apfs_o.o_xid, &m_omap))
		std::cerr << "ERROR: root tree init failed" << std::endl;

	if (!m_extentref_tree.Init(m_sb.apfs_extentref_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: extentref tree init failed" << std::endl;

	if (!m_snap_meta_tree.Init(m_sb.apfs_snap_meta_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: snap meta tree init failed" << std::endl;

	if (m_sb.apfs_incompatible_features & APFS_INCOMPAT_SEALED_VOLUME)
	{
		if (!m_fext_tree.Init(m_sb.apfs_fext_tree_oid, m_sb.apfs_o.o_xid))
			std::cerr << "ERROR: fext tree init failed" << std::endl;
	}

	return true;
}

bool ApfsVolume::MountSnapshot(paddr_t apsb_paddr, xid_t snap_xid)
{
	BTree snap_btree(m_container);
	BTreeEntry snap_entry;
	j_snap_metadata_key_t snap_key;
	const j_snap_metadata_val_t *snap_val = nullptr;
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

	if (m_sb.apfs_snap_meta_tree_oid == 0)
		return false;

	if (!snap_btree.Init(m_sb.apfs_snap_meta_tree_oid, m_sb.apfs_o.o_xid)) {
		std::cerr << "snap meta tree init failed" << std::endl;
		return false;
	}

	snap_key.hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_SNAP_METADATA, snap_xid);
	if (!snap_btree.Lookup(snap_entry, &snap_key, sizeof(snap_key), CompareSnapMetaKey, nullptr, true)) {
		std::cerr << "snap xid not found" << std::endl;
		return false;
	}

	snap_val = reinterpret_cast<const j_snap_metadata_val_t *>(snap_entry.val);

	if (!m_omap.Init(m_sb.apfs_omap_oid, m_sb.apfs_o.o_xid)) {
		std::cerr << "WARNING: Volume omap tree init failed." << std::endl;
		return false;
	}

	if (!ReadBlocks(blk.data(), snap_val->sblock_oid, 1, 0)) {
		std::cerr << "failed to read snapshot superblock" << std::endl;
		return false;
	}
	if (!VerifyBlock(blk.data(), blk.size())) {
		std::cerr << "snap superblock checksum error" << std::endl;
		return false;
	}

	memcpy(&m_sb, blk.data(), sizeof(m_sb));

	if (m_sb.apfs_magic != APFS_MAGIC)
		return false;

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

	if (!m_fs_tree.Init(m_sb.apfs_root_tree_oid, m_sb.apfs_o.o_xid, &m_omap))
		std::cerr << "WARNING: root tree init failed" << std::endl;

	if (!m_extentref_tree.Init(m_sb.apfs_extentref_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: extentref tree init failed" << std::endl;

	if (!m_snap_meta_tree.Init(m_sb.apfs_snap_meta_tree_oid, m_sb.apfs_o.o_xid))
		std::cerr << "WARNING: snap meta tree init failed" << std::endl;

	if (m_sb.apfs_incompatible_features & APFS_INCOMPAT_SEALED_VOLUME)
	{
		if (!m_fext_tree.Init(m_sb.apfs_fext_tree_oid, m_sb.apfs_o.o_xid))
			std::cerr << "ERROR: fext tree init failed" << std::endl;
	}

	return true;
}

void ApfsVolume::dump(BlockDumper& bd)
{
	std::vector<uint8_t> blk;
	omap_res_t om;
	oid_t omap_snapshot_tree_oid = 0;

	blk.resize(m_container.GetBlocksize());

	if (!ReadBlocks(blk.data(), m_apsb_paddr, 1, 0))
		return;

	if (!VerifyBlock(blk.data(), blk.size()))
		return;

	bd.SetTextFlags(m_sb.apfs_incompatible_features & 0xFF);

	bd.DumpNode(blk.data(), m_apsb_paddr);

	ReadBlocks(blk.data(), m_sb.apfs_omap_oid, 1, 0);
	bd.DumpNode(blk.data(), m_sb.apfs_omap_oid);

	{
		const omap_phys_t *om = reinterpret_cast<const omap_phys_t*>(blk.data());
		omap_snapshot_tree_oid = om->om_snapshot_tree_oid;

		ReadBlocks(blk.data(), omap_snapshot_tree_oid, 1, 0);
		bd.DumpNode(blk.data(), omap_snapshot_tree_oid);
	}

	if (m_sb.apfs_er_state_oid) {
		ReadBlocks(blk.data(), m_sb.apfs_er_state_oid, 1, 0);
		bd.DumpNode(blk.data(), m_sb.apfs_er_state_oid);
	}

	m_omap.dump(bd);
	m_fs_tree.dump(bd);
	// m_extentref_tree.dump(bd);
	m_snap_meta_tree.dump(bd);

	if (m_sb.apfs_integrity_meta_oid != 0) {
		if (m_omap.Lookup(om, m_sb.apfs_integrity_meta_oid, m_sb.apfs_o.o_xid))
		{
			ReadBlocks(blk.data(), om.paddr, 1, 0);
			bd.DumpNode(blk.data(), om.paddr);
		}
	}

	if (m_sb.apfs_snap_meta_ext_oid != 0) {
		if (m_omap.Lookup(om, m_sb.apfs_snap_meta_ext_oid, m_sb.apfs_o.o_xid))
		{
			ReadBlocks(blk.data(), om.paddr, 1, 0);
			bd.DumpNode(blk.data(), om.paddr);
		}
	}

#if 1
	if (m_sb.apfs_fext_tree_oid != 0) {
		BTree fxtree(m_container, this);
		fxtree.Init(m_sb.apfs_fext_tree_oid, m_sb.apfs_o.o_xid);
		fxtree.dump(bd);
	}
#endif

	BTreeEntry bte;
#if 0
	BTreeIterator it;
	const j_snap_metadata_key_t *sm_key;
	const j_snap_metadata_val_t *sm_val;

	if (m_snap_meta_tree.GetIteratorBegin(it)) {
		for (;;) {
			if (!it.GetEntry(bte)) break;

			sm_key = reinterpret_cast<const j_snap_metadata_key_t *>(bte.key);
			sm_val = reinterpret_cast<const j_snap_metadata_val_t *>(bte.val);

			if ((sm_key->hdr.obj_id_and_type >> OBJ_TYPE_SHIFT) != APFS_TYPE_SNAP_METADATA) break;

			ReadBlocks(blk.data(), sm_val->sblock_oid, 1, 0);
			bd.DumpNode(blk.data(), sm_val->sblock_oid);

			apfs_superblock_t apsb;
			memcpy(&apsb, blk.data(), sizeof(apfs_superblock_t));

			if (apsb.apfs_omap_oid) {
				ReadBlocks(blk.data(), apsb.apfs_omap_oid, 1, 0);
				bd.DumpNode(blk.data(), apsb.apfs_omap_oid);
			}

			if (!it.next()) break;
		}
	}

	bte.clear();
#endif

#if 0
	{
		BTree omap_tree(m_container, this);
		BTreeIterator oit;
		omap_phys_t om;

		ReadBlocks(blk.data(), m_sb.apfs_omap_oid, 1, 0);
		memcpy(&om, blk.data(), sizeof(om));

		omap_tree.Init(om.om_tree_oid, om.om_o.o_xid);
		if (omap_tree.GetIteratorBegin(oit)) {
			const omap_key_t *ok;
			const omap_val_t *ov;
			for (;;) {
				if (!oit.GetEntry(bte)) break;
				ok = reinterpret_cast<const omap_key_t*>(bte.key);
				ov = reinterpret_cast<const omap_val_t*>(bte.val);

				bd.st() << "omap: " << ok->ok_oid << " " << ok->ok_xid << " => " << ov->ov_flags << " " << ov->ov_size << " " << ov->ov_paddr << std::endl;
				if (ov->ov_flags & OMAP_VAL_NOHEADER) {
					ReadBlocks(blk.data(), ov->ov_paddr, 1, 0);
					bd.DumpNode(blk.data(), ov->ov_paddr);
				}

				if (!oit.next()) break;
			}
		} else {
			bd.st() << "Failed getting omap iterator" << std::endl;
		}
	}
#endif
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

int ApfsVolume::CompareSnapMetaKey(const void* skey, size_t skey_len, const void* ekey, size_t ekey_len, void* context)
{
	const j_key_t *ks = reinterpret_cast<const j_key_t*>(skey);
	const j_key_t *ke = reinterpret_cast<const j_key_t*>(ekey);
	const j_snap_name_key_t *sks;
	const j_snap_name_key_t *ske;

	if (ke->obj_id_and_type < ks->obj_id_and_type)
		return -1;
	if (ke->obj_id_and_type > ks->obj_id_and_type)
		return 1;

	switch (ks->obj_id_and_type >> OBJ_TYPE_SHIFT)
	{
	case APFS_TYPE_SNAP_METADATA:
		break;
	case APFS_TYPE_SNAP_NAME:
		sks = reinterpret_cast<const j_snap_name_key_t*>(skey);
		ske = reinterpret_cast<const j_snap_name_key_t*>(ekey);
		return apfs_strncmp(ske->name, ske->name_len, sks->name, sks->name_len);
		break;
	}

	return 0;
}

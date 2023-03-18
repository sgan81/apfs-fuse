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
#include <cassert>
#include <cinttypes>

#include "Global.h"

#include "Container.h"
#include "Volume.h"
#include "ObjCache.h"
#include "BlockDumper.h"
#include "Util.h"
#include "ApfsDir.h"
#include "OMap.h"
#include "Debug.h"

int CompareFsKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res)
{
	// assert(skey_len == 8);
	// assert(ekey_len == 8);

	(void)ekey_len;

	uint64_t ks = *reinterpret_cast<const le_uint64_t *>(skey);
	uint64_t ke = *reinterpret_cast<const le_uint64_t *>(ekey);

	ks = (ks << 4) | (ks >> 60);
	ke = (ke << 4) | (ke >> 60);

	if (ke < ks) {
		res = -1;
		return 0;
	}
	else if (ke > ks) {
		res = 1;
		return 0;
	}

	if (skey_len > 8)
	{
		switch (ks & 0xF)
		{
			case APFS_TYPE_DIR_REC:
				if (context & (APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_NORMALIZATION_INSENSITIVE))
				{
					const j_drec_hashed_key_t *s = reinterpret_cast<const j_drec_hashed_key_t *>(skey);
					const j_drec_hashed_key_t *e = reinterpret_cast<const j_drec_hashed_key_t *>(ekey);

					/* TODO: Is this correct? Yes. TODO check _apfs_cstrncmp. */
					if ((e->name_len_and_hash & J_DREC_HASH_MASK) < (s->name_len_and_hash & J_DREC_HASH_MASK)) {
						res = -1;
						return 0;
					}
					else if ((e->name_len_and_hash & J_DREC_HASH_MASK) > (s->name_len_and_hash & J_DREC_HASH_MASK)) {
						res = 1;
						return 0;
					}

					res = apfs_strncmp(e->name, e->name_len_and_hash & J_DREC_LEN_MASK, s->name, s->name_len_and_hash & J_DREC_LEN_MASK);
					return 0;
				}
				else
				{
					const j_drec_key_t *s = reinterpret_cast<const j_drec_key_t *>(skey);
					const j_drec_key_t *e = reinterpret_cast<const j_drec_key_t *>(ekey);

					res = apfs_strncmp(e->name, e->name_len, s->name, s->name_len);
					return 0;
				}
				break;
			case APFS_TYPE_FILE_EXTENT:
			{
				const j_file_extent_key_t *s = reinterpret_cast<const j_file_extent_key_t *>(skey);
				const j_file_extent_key_t *e = reinterpret_cast<const j_file_extent_key_t *>(ekey);

				assert(skey_len == sizeof(j_file_extent_key_t));
				assert(ekey_len == sizeof(j_file_extent_key_t));

				if (e->logical_addr < s->logical_addr) {
					res = -1;
					return 0;
				}
				else if (e->logical_addr > s->logical_addr) {
					res = 1;
					return 0;
				}
				res = 0;
				return 0;
				break;
			}
			case APFS_TYPE_XATTR:
			{
				const j_xattr_key_t *s = reinterpret_cast<const j_xattr_key_t *>(skey);
				const j_xattr_key_t *e = reinterpret_cast<const j_xattr_key_t *>(ekey);

				res = apfs_strncmp(e->name, e->name_len, s->name, s->name_len);
				return 0;
				break;
			}
			case APFS_TYPE_SNAP_NAME:
			{
				const j_snap_name_key_t* s = reinterpret_cast<const j_snap_name_key_t*>(skey);
				const j_snap_name_key_t* e = reinterpret_cast<const j_snap_name_key_t*>(ekey);
				res = apfs_strncmp(e->name, e->name_len, s->name, s->name_len);
				return 0;
				break;
			}
			case APFS_TYPE_FILE_INFO:
			{
				const j_file_info_key_t *s = reinterpret_cast<const j_file_info_key_t *>(skey);
				const j_file_info_key_t *e = reinterpret_cast<const j_file_info_key_t *>(ekey);

				if (e->info_and_lba < s->info_and_lba)
					res = -1;
				else if (e->info_and_lba > s->info_and_lba)
					res = 1;
				else
					res = 0;
				return 0;
				break;
			}
		}
	}

	res = 0;
	return 0;
}

int CompareFextKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res)
{
	const fext_tree_key_t *s = reinterpret_cast<const fext_tree_key_t *>(skey);
	const fext_tree_key_t *e = reinterpret_cast<const fext_tree_key_t *>(ekey);

	if (e->private_id < s->private_id)
		res = -1;
	else if (e->private_id > s->private_id)
		res = 1;
	else if (e->logical_addr < s->logical_addr)
		res = -1;
	else if (e->logical_addr > s->logical_addr)
		res = 1;
	else
		res = 0;
	return 0;
}

Volume::Volume(Container &container) :
	m_container(container)
{
	m_is_encrypted = false;
}

Volume::~Volume()
{
}

int Volume::init(const void* params)
{
	(void)params;
	m_sb = reinterpret_cast<const apfs_superblock_t*>(data());
	if (m_sb->apfs_magic != APFS_MAGIC)
		return EINVAL;

	return 0;
}

int Volume::Mount()
{
	int err;

	log_debug("init omap ...\n");
	err = oc().getObj(m_omap, nullptr, m_sb->apfs_omap_oid, 0, OBJ_PHYSICAL | OBJECT_TYPE_OMAP, 0, 0, 0, nullptr);
	if (err) {
		log_error("mount: omap init failed, err = %d\n", err);
		return err;
	}
	// m_omap->dump_tree();

	if ((m_sb->apfs_fs_flags & 3) != APFS_FS_UNENCRYPTED && !m_container.IsUnencrypted())
	{
		uint8_t vek[0x20];
		std::string str;

		log_debug("Volume is encrypted.\n");

		err = m_container.GetVolumeKey(vek, m_sb->apfs_vol_uuid);
		if (err) {
			printf("Volume %s is encrypted.\n", m_sb->apfs_volname);
			if (m_container.GetPasswordHint(str, m_sb->apfs_vol_uuid))
				printf("Hint: %s\n", str.c_str());

			printf("Enter password: ");
			GetPassword(str);

			err = m_container.GetVolumeKey(vek, m_sb->apfs_vol_uuid, str.c_str());
			if (err) {
				printf("Wrong password!\n");
				return EINVAL;
			}
		}

		log_debug("Setting VEK\n");
		m_aes.SetKey(vek, vek + 0x10);
		m_is_encrypted = true;
	}

	log_debug("init fs tree %" PRIx64 " ...\n", m_sb->apfs_root_tree_oid);
	err = m_fs_tree.Init(this, m_sb->apfs_root_tree_oid, 0, m_sb->apfs_root_tree_type, OBJECT_TYPE_FSTREE,
		CompareFsKey, m_sb->apfs_incompatible_features & (APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_NORMALIZATION_INSENSITIVE));
	if (err) {
		log_error("mount: fs tree init failed.\n");
		return EINVAL;
	}

	err = m_extentref_tree.Init(this, m_sb->apfs_extentref_tree_oid, 0, m_sb->apfs_extentref_tree_type, OBJECT_TYPE_EXTENT_LIST_TREE, CompareFsKey, 0);
	if (err)
		log_error("mount: extentref tree init failed.\n");

	err = m_snap_meta_tree.Init(this, m_sb->apfs_snap_meta_tree_oid, 0, m_sb->apfs_snap_meta_tree_type, OBJECT_TYPE_SNAPMETATREE, CompareFsKey, 0);
	if (err)
		log_error("mount: snap meta tree init failed.\n");

	if (m_sb->apfs_incompatible_features & APFS_INCOMPAT_SEALED_VOLUME) {
		log_debug("init fext tree ...\n");
		err = m_fext_tree.Init(this, m_sb->apfs_fext_tree_oid, 0, m_sb->apfs_fext_tree_type, OBJECT_TYPE_FEXT_TREE, CompareFextKey, 0);
		if (err) {
			log_error("mount: fext tree init failed.\n");
			return EINVAL;
		}
	}

	return 0;
}

int Volume::MountSnapshot(paddr_t apsb_paddr, xid_t snap_xid)
{
	// Currently not supported
#if 0
	const apfs_superblock_t* apsb;
	BTree snap_btree;
	j_snap_metadata_key_t snap_key;
	union {
		uint8_t buf[JOBJ_MAX_VALUE_SIZE];
		j_snap_metadata_val_t v;
	} snap_val;
	uint16_t sk_len = sizeof(snap_key);
	uint16_t sv_len = sizeof(snap_val);

	std::vector<uint8_t> blk;
	int err;

	blk.resize(m_container.GetBlocksize());

	if (!ReadBlocks(blk.data(), apsb_paddr, 1, 0))
		return EIO;

	if (!VerifyBlock(blk.data(), blk.size()))
		return EINVAL;

	apsb = reinterpret_cast<const apfs_superblock_t*>(blk.data());

	if (apsb->apfs_magic != APFS_MAGIC) {
		log_error("Mount snapshot: not an apfs superblock.\n");
		return EINVAL;
	}

	if (apsb->apfs_snap_meta_tree_oid == 0) {
		log_error("Mount snapshot: no snapshot meta tree there.\n");
		return EINVAL;
	}

	if (!snap_btree.Init(this, apsb->apfs_snap_meta_tree_oid, 0, apsb->apfs_snap_meta_tree_type, OBJECT_TYPE_SNAPMETATREE, CompareFsKey, 0)) {
		log_error("snap meta tree init failed.\n");
		return false;
	}

	snap_key.hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_SNAP_METADATA, snap_xid);
	err = snap_btree.Lookup(&snap_key, sizeof(snap_key), sk_len, &snap_val.buf, sv_len, BTree::FindMode::EQ);
	if (err != 0) {
		log_error("error %d looking up snap xid.\n", err);
		return false;
	}

	if (!m_omap.Init(m_sb.apfs_omap_oid, m_sb.apfs_o.o_xid)) {
		std::cerr << "WARNING: Volume omap tree init failed." << std::endl;
		return false;
	}

	if (!ReadBlocks(blk.data(), snap_val.v.sblock_oid, 1, 0)) {
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

	if (!m_fs_tree.Init(m_sb.apfs_root_tree_oid, m_sb.apfs_o.o_xid, CompareStdDirKey, &m_sb, &m_omap))
		std::cerr << "WARNING: root tree init failed" << std::endl;

	if (!m_extentref_tree.Init(m_sb.apfs_extentref_tree_oid, m_sb.apfs_o.o_xid, CompareStdDirKey, &m_sb))
		std::cerr << "WARNING: extentref tree init failed" << std::endl;

	if (!m_snap_meta_tree.Init(m_sb.apfs_snap_meta_tree_oid, m_sb.apfs_o.o_xid, CompareSnapMetaKey, nullptr))
		std::cerr << "WARNING: snap meta tree init failed" << std::endl;

	if (m_sb.apfs_incompatible_features & APFS_INCOMPAT_SEALED_VOLUME)
	{
		if (!m_fext_tree.Init(m_sb.apfs_fext_tree_oid, m_sb.apfs_o.o_xid, CompareFextKey, nullptr))
			std::cerr << "ERROR: fext tree init failed" << std::endl;
	}
#endif
	return ENOTSUP;
}

int Volume::getOMap(ObjPtr<OMap>& omap)
{
	if (!(m_omap)) {
		int err = oc().getObj(m_omap, nullptr, m_sb->apfs_omap_oid, 0, OBJ_PHYSICAL | OBJECT_TYPE_OMAP, 0, 0, 0, nullptr);
		if (err) {
			log_error("Volume: Loading omap failed, err = %d\n", err);
			 return err;
		}
	}
	omap = m_omap;
	return 0;
}

void Volume::dump(BlockDumper& bd)
{
#if 0 // <--
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
		fxtree.Init(m_sb.apfs_fext_tree_oid, m_sb.apfs_o.o_xid, CompareFextKey, nullptr);
		fxtree.dump(bd);
	}
#endif
#endif // <--

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

int Volume::ReadBlocks(uint8_t * data, paddr_t paddr, uint64_t blkcnt, uint64_t xts_tweak)
{
	int err;
	constexpr int encryption_block_size = 0x200;

	err = m_container.ReadBlocks(data, paddr, blkcnt);
	if (err) return err;

	if (!m_is_encrypted || (xts_tweak == 0))
		return 0;

	uint64_t cs_factor = m_container.GetBlocksize() / encryption_block_size;
	uint64_t uno = xts_tweak * cs_factor;
	size_t size = blkcnt * m_container.GetBlocksize();
	size_t k;

	for (k = 0; k < size; k += encryption_block_size)
	{
		m_aes.Decrypt(data + k, data + k, encryption_block_size, uno);
		uno++;
	}

	return 0;
}

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

#include <iostream>
#include <iomanip>
#include <algorithm>

#include <cassert>
#include <cstring>

#include "ApfsDir.h"
#include "ApfsVolume.h"
#include "ApfsContainer.h"
#include "BTree.h"
#include "Util.h"

#ifndef _MSC_VER
template<size_t L>
void strcpy_s(char (&dst)[L], const char *src)
{
	strncpy(dst, src, L - 1);
	dst[L - 1] = 0;
}
#endif

ApfsDir::Inode::Inode()
{
	obj_id = 0;

	parent_id = 0;
	private_id = 0;

	create_time = 0;
	mod_time = 0;
	change_time = 0;
	access_time = 0;

	internal_flags = 0;

	nchildren_nlink = 0;

	default_protection_class = 0;
	write_generation_counter = 0;
	bsd_flags = 0;
	owner = 0;
	group = 0;
	mode = 0;

	uncompressed_size = 0;

	snap_xid = 0;
	delta_tree_oid = 0;
	prev_fsize = 0;

	ds_size = 0;
	ds_alloced_size = 0;
	ds_default_crypto_id = 0;
	ds_total_bytes_written = 0;
	ds_total_bytes_read = 0;

	memset(fs_uuid, 0, sizeof(fs_uuid));

	sparse_bytes = 0;
	document_id = 0;
	rdev = 0;

	optional_present_flags = 0;

	/*
	ino = nullptr;
	*/
}

ApfsDir::Inode::Inode(const ApfsDir::Inode& o)
{
	obj_id = o.obj_id;

	parent_id = o.parent_id;
	private_id = o.private_id;

	create_time = o.create_time;
	mod_time = o.mod_time;
	change_time = o.change_time;
	access_time = o.access_time;

	internal_flags = o.internal_flags;

	nchildren_nlink = o.nchildren_nlink;

	default_protection_class = o.default_protection_class;
	write_generation_counter = o.write_generation_counter;
	bsd_flags = o.bsd_flags;
	owner = o.owner;
	group = o.group;
	mode = o.mode;

	uncompressed_size = o.uncompressed_size;

	snap_xid = o.snap_xid;
	delta_tree_oid = o.delta_tree_oid;
	prev_fsize = o.prev_fsize;
	// FinderInfo
	ds_size = o.ds_size;
	ds_alloced_size = o.ds_alloced_size;
	ds_default_crypto_id = o.ds_default_crypto_id;
	ds_total_bytes_written = o.ds_total_bytes_written;
	ds_total_bytes_read = o.ds_total_bytes_read;
	// j_dir_stats_val_t dir_stats;
	memcpy(fs_uuid, o.fs_uuid, sizeof(apfs_uuid_t));
	sparse_bytes = o.sparse_bytes;
	document_id = o.document_id;;
	rdev = o.rdev;
	name = o.name;

	optional_present_flags = o.optional_present_flags;

	/*
	ino_val_data = o.ino_val_data;
	ino = reinterpret_cast<j_inode_val_t *>(ino_val_data.data());
	*/
}

ApfsDir::DirRec::DirRec()
{
	parent_id = 0;
	hash = 0;

	file_id = 0;
	date_added = 0;

	sibling_id = 0;
	flags = 0;
	has_sibling_id = false;
}

ApfsDir::DirRec::DirRec(const ApfsDir::DirRec& o)
{
	parent_id = o.parent_id;
	hash = o.hash;
	name = o.name;

	file_id = o.file_id;
	date_added = o.date_added;

	sibling_id = o.sibling_id;
	flags = o.flags;
	has_sibling_id = o.has_sibling_id;
}

ApfsDir::ApfsDir(ApfsVolume &vol) :
	m_vol(vol),
	m_fs_tree(vol.fstree())
{
	m_txt_fmt = vol.getTextFormat();

	m_blksize = vol.getContainer().GetBlocksize();
	m_blksize_sh = log2(m_blksize);
	m_blksize_mask_lo = m_blksize - 1;
	m_blksize_mask_hi = ~m_blksize_mask_lo;
	m_tmp_blk.resize(m_blksize);
	// m_bt.EnableDebugOutput();
}

ApfsDir::~ApfsDir()
{
}

bool ApfsDir::GetInode(ApfsDir::Inode& res, uint64_t inode)
{
	BTreeEntry bte;
	j_inode_key_t key;
	bool rc;

	key.hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_INODE, inode);

	rc = m_fs_tree.Lookup(bte, &key, sizeof(j_inode_key_t), CompareStdDirKey, this, true);

	if (!rc || (bte.val == nullptr))
		return false;

	// const uint8_t *idata = reinterpret_cast<const uint8_t *>(bte.val);
	const j_inode_val_t *obj = reinterpret_cast<const j_inode_val_t *>(bte.val);

	res.obj_id = inode;

	res.parent_id = obj->parent_id;
	res.private_id = obj->private_id;

	res.create_time = obj->create_time;
	res.mod_time = obj->mod_time;
	res.change_time = obj->change_time;
	res.access_time = obj->access_time;

	res.internal_flags = obj->internal_flags;

	res.nchildren_nlink = obj->nchildren;

	res.default_protection_class = obj->default_protection_class;
	res.write_generation_counter = obj->write_generation_counter;
	res.bsd_flags = obj->bsd_flags;
	res.owner = obj->owner;
	res.group = obj->group;
	res.mode = obj->mode;

	// internal_flags & 0x00200000 => pad2 = uncompressed_size ?

	if (bte.val_len > sizeof(j_inode_val_t))
	{
		const xf_blob_t *xf_hdr = reinterpret_cast<const xf_blob_t *>(obj->xfields);
		const x_field_t *xf = reinterpret_cast<const x_field_t *>(xf_hdr->xf_data);
		const uint8_t *xdata = obj->xfields + sizeof(xf_blob_t) + xf_hdr->xf_num_exts * sizeof(x_field_t);
		uint16_t n;

		for (n = 0; n < xf_hdr->xf_num_exts; n++)
		{
			switch (xf[n].x_type)
			{
			case INO_EXT_TYPE_SNAP_XID:
				assert(xf[n].x_size == sizeof(uint64_t));
				res.snap_xid = bswap_le(*reinterpret_cast<const uint64_t *>(xdata));
				res.optional_present_flags |= Inode::INO_HAS_SNAP_XID;
				break;
			case INO_EXT_TYPE_DELTRA_TREE_OID:
				assert(xf[n].x_size == sizeof(uint64_t));
				res.delta_tree_oid = bswap_le(*reinterpret_cast<const uint64_t *>(xdata));
				res.optional_present_flags |= Inode::INO_HAS_DELTA_TREE_OID;
				break;
			case INO_EXT_TYPE_DOCUMENT_ID:
				assert(xf[n].x_size == sizeof(uint32_t));
				res.document_id = bswap_le(*reinterpret_cast<const uint32_t *>(xdata));
				res.optional_present_flags |= Inode::INO_HAS_DOCUMENT_ID;
				break;
			case INO_EXT_TYPE_NAME:
				res.name.assign(reinterpret_cast<const char *>(xdata), xf[n].x_size);
				res.optional_present_flags |= Inode::INO_HAS_NAME;
				break;
			case INO_EXT_TYPE_PREV_FSIZE:
				assert(xf[n].x_size == sizeof(uint64_t));
				res.prev_fsize = bswap_le(*reinterpret_cast<const uint64_t *>(xdata));
				res.optional_present_flags |= Inode::INO_HAS_PREV_FSIZE;
				break;
			case INO_EXT_TYPE_FINDER_INFO:
				// TODO
				break;
			case INO_EXT_TYPE_DSTREAM:
			{
				assert(xf[n].x_size == sizeof(j_dstream_t));
				const j_dstream_t *ds = reinterpret_cast<const j_dstream_t *>(xdata);
				res.ds_size = ds->size;
				res.ds_alloced_size = ds->alloced_size;
				res.ds_default_crypto_id = ds->default_crypto_id;
				res.ds_total_bytes_written = ds->total_bytes_written;
				res.ds_total_bytes_read = ds->total_bytes_read;
				res.optional_present_flags |= Inode::INO_HAS_DSTREAM;
			}
				break;
			case INO_EXT_TYPE_DIR_STATS_KEY:
				// TODO
				break;
			case INO_EXT_TYPE_FS_UUID:
				assert(xf[n].x_size == sizeof(apfs_uuid_t));
				memcpy(res.fs_uuid, xdata, sizeof(apfs_uuid_t));
				res.optional_present_flags |= Inode::INO_HAS_FS_UUID;
				break;
			case INO_EXT_TYPE_SPARSE_BYTES:
				assert(xf[n].x_size == sizeof(uint64_t));
				res.sparse_bytes = bswap_le(*reinterpret_cast<const uint64_t *>(xdata));
				res.optional_present_flags |= Inode::INO_HAS_SPARSE_BYTES;
				break;
			default:
				std::cerr << "Warning: Unknown XF " << xf[n].x_type << " at inode " << inode << std::endl;
				break;
			}

			xdata += ((xf[n].x_size + 7) & ~7);
		}
	}

	return true;
}

bool ApfsDir::ListDirectory(std::vector<DirRec> &dir, uint64_t inode)
{
	uint8_t skey_buf[0x500];

	BTreeIterator it;
	BTreeEntry bte;
	bool rc;
	uint64_t skey;

	const j_key_t *k;
	const j_drec_val_t *v;

	skey = APFS_TYPE_ID(APFS_TYPE_DIR_REC, inode);

	dir.clear();

	if (m_txt_fmt & 9)
	{
		j_drec_hashed_key_t *key = reinterpret_cast<j_drec_hashed_key_t *>(skey_buf);
		key->hdr.obj_id_and_type = skey;
		key->name_len_and_hash = 0;
		key->name[0] = 0;

		rc = m_fs_tree.GetIterator(it, key, sizeof(j_drec_hashed_key_t), CompareStdDirKey, this);
	}
	else
	{
		j_drec_key_t *key = reinterpret_cast<j_drec_key_t *>(skey_buf);
		key->hdr.obj_id_and_type = skey;
		key->name_len = 0;
		key->name[0] = 0;

		rc = m_fs_tree.GetIterator(it, key, sizeof(j_drec_key_t), CompareStdDirKey, this);
	}

	if (!rc)
		return false;

	for (;;)
	{
		DirRec e;

		rc = it.GetEntry(bte);
		if (!rc)
			break;

		if (g_debug & Dbg_Dir)
		{
			DumpBuffer(reinterpret_cast<const uint8_t *>(bte.key), bte.key_len, "entry key");
			DumpBuffer(reinterpret_cast<const uint8_t *>(bte.val), bte.val_len, "entry val");
		}

		k = reinterpret_cast<const j_key_t *>(bte.key);

		if (k->obj_id_and_type != skey)
			break;

		e.parent_id = k->obj_id_and_type & OBJ_ID_MASK;

		if (m_txt_fmt != 0)
		{
			const j_drec_hashed_key_t *hk = reinterpret_cast<const j_drec_hashed_key_t *>(bte.key);
			e.hash = hk->name_len_and_hash;
			e.name = reinterpret_cast<const char *>(hk->name);
		}
		else
		{
			const j_drec_key_t *rk = reinterpret_cast<const j_drec_key_t *>(bte.key);
			e.hash = 0;
			e.name = reinterpret_cast<const char *>(rk->name);
		}

		// assert(res.val_len == sizeof(APFS_Name));

		v = reinterpret_cast<const j_drec_val_t *>(bte.val);

		e.file_id = v->file_id;
		e.date_added = v->date_added;
		e.flags = v->flags;

		if (bte.val_len > sizeof(j_drec_val_t))
		{
			const xf_blob_t *xf_hdr = reinterpret_cast<const xf_blob_t *>(v->xfields);
			const x_field_t *xf = reinterpret_cast<const x_field_t *>(xf_hdr->xf_data);
			const uint8_t *xdata = v->xfields + sizeof(xf_blob_t) + xf_hdr->xf_num_exts * sizeof(x_field_t);
			uint16_t n;

			for (n = 0; n < xf_hdr->xf_num_exts; n++)
			{
				switch (xf[n].x_type)
				{
				case DREC_EXT_TYPE_SIBLING_ID:
					assert(xf[n].x_size == sizeof(uint64_t));
					e.sibling_id = bswap_le(*reinterpret_cast<const uint64_t *>(xdata));
					e.has_sibling_id = true;
					break;
				default:
					std::cerr << "Warning: Unknown XF " << xf[n].x_type << " at drec " << e.file_id << std::endl;
					break;
				}

				xdata += ((xf[n].x_size + 7) & ~7);
			}
		}

		dir.push_back(e);

		it.next();
	}

	return true;
}

bool ApfsDir::LookupName(ApfsDir::DirRec& res, uint64_t parent_id, const char* name)
{
	bool rc;
	BTreeEntry e;
	uint8_t srch_key_buf[0x500];
	size_t name_len = strlen(name) + 1;

	if (name_len > 0x400)
		return false;

	res.parent_id = parent_id;
	res.name = name;

	if (m_txt_fmt & 9)
	{
		j_drec_hashed_key_t *skey = reinterpret_cast<j_drec_hashed_key_t *>(srch_key_buf);

		skey->hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_DIR_REC, parent_id);
		skey->name_len_and_hash = HashFilename(reinterpret_cast<const uint8_t *>(name), static_cast<uint16_t>(name_len), (m_txt_fmt & APFS_INCOMPAT_CASE_INSENSITIVE) != 0);
		memcpy(skey->name, name, name_len);
		if (g_debug & Dbg_Dir)
		{
			std::cout << "Lookup hashed key: key=" << skey->hdr.obj_id_and_type << " hash=" << skey->name_len_and_hash << " name='" << reinterpret_cast<const char *>(skey->name) << "'" << std::endl;
			dump_utf8(std::cout, skey->name);
		}
		res.hash = skey->name_len_and_hash;

		rc = m_fs_tree.Lookup(e, skey, sizeof(j_drec_hashed_key_t) + (skey->name_len_and_hash & J_DREC_LEN_MASK), CompareStdDirKey, this, true);
	}
	else
	{
		j_drec_key_t *skey = reinterpret_cast<j_drec_key_t *>(srch_key_buf);
		skey->hdr.obj_id_and_type = parent_id | (static_cast<uint64_t>(APFS_TYPE_DIR_REC) << OBJ_TYPE_SHIFT);
		skey->name_len = static_cast<uint16_t>(name_len);
		memcpy(skey->name, name, name_len);
		if (g_debug & Dbg_Dir)
		{
			std::cout << "Lookup key: key=" << skey->hdr.obj_id_and_type << " name_len=" << skey->name_len << " name='" << reinterpret_cast<const char *>(skey->name) << "'" << std::endl;
			dump_utf8(std::cout, skey->name);
		}
		res.hash = 0;

		rc = m_fs_tree.Lookup(e, skey, sizeof(j_drec_key_t) + skey->name_len, CompareStdDirKey, this, true);
	}

	if (!rc)
	{
		if (g_debug & Dbg_Dir)
			std::cout << "Lookup failed!" << std::endl;
		return false;
	}

	const j_drec_val_t *v = reinterpret_cast<const j_drec_val_t *>(e.val);

	res.file_id = v->file_id;
	res.date_added = v->date_added;
	// v->unk ?
	// TODO: SIBLING-ID! XF!

	if (g_debug & Dbg_Dir)
		std::cout << "Lookup: id = " << res.file_id << std::endl;

	return true;
}

bool ApfsDir::ReadFile(void* data, uint64_t inode, uint64_t offs, size_t size)
{
	BTreeEntry e;
	bool rc;

	uint8_t *bdata = reinterpret_cast<uint8_t *>(data);

	size_t cur_size;
	uint64_t blk_idx;
	uint64_t blk_offs;

	uint64_t extent_size;
	uint64_t extent_offs;
	paddr_t extent_paddr;
	uint64_t extent_crypto_id;

	while (size > 0)
	{
		if (m_vol.isSealed()) {
			fext_tree_key_t key;
			const fext_tree_key_t *fext_key = nullptr;
			const fext_tree_val_t *fext_val = nullptr;

			key.private_id = inode;
			key.logical_addr = offs;

			rc = m_vol.fexttree().Lookup(e, &key, sizeof(key), CompareFextKey, this, false);
			if (!rc) return false;

			fext_key = reinterpret_cast<const fext_tree_key_t *>(e.key);
			fext_val = reinterpret_cast<const fext_tree_val_t *>(e.val);

			extent_offs = offs - fext_key->logical_addr;

			extent_size = fext_val->len_and_flags & J_FILE_EXTENT_LEN_MASK;
			extent_paddr = fext_val->phys_block_num;
			extent_crypto_id = 0; /* TODO: Crypto on sealed volumes? Need later beta for that ... */
		} else {
			j_file_extent_key_t key;
			const j_file_extent_key_t *ext_key = nullptr;
			const j_file_extent_val_t *ext_val = nullptr;

			key.hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_FILE_EXTENT, inode);
			key.logical_addr = offs;

			if (g_debug & Dbg_Dir)
				std::cout << "ReadFile(inode=" << inode << ",offs=" << offs << ",size=" << size << ")" << std::endl;

			rc = m_fs_tree.Lookup(e, &key, sizeof(key), CompareStdDirKey, this, false);

			if (!rc)
				return false;

			ext_key = reinterpret_cast<const j_file_extent_key_t *>(e.key);
			ext_val = reinterpret_cast<const j_file_extent_val_t *>(e.val);

			if (g_debug & Dbg_Dir)
			{
				std::cout << "FileExtent " << ext_key->hdr.obj_id_and_type << " " << ext_key->logical_addr << " => ";
				std::cout << ext_val->len_and_flags << " " << ext_val->phys_block_num << " " << ext_val->crypto_id << std::endl;
			}

			if (ext_key->hdr.obj_id_and_type != key.hdr.obj_id_and_type)
				return false;

			// Remove flags from length member
			extent_offs = offs - ext_key->logical_addr;

			extent_size = ext_val->len_and_flags & J_FILE_EXTENT_LEN_MASK;
			extent_paddr = ext_val->phys_block_num;
			extent_crypto_id = ext_val->crypto_id;
		}

		blk_idx = extent_offs >> m_blksize_sh;
		blk_offs = extent_offs & m_blksize_mask_lo;

		cur_size = size;

		if ((extent_offs + cur_size) > extent_size)
			cur_size = extent_size - extent_offs;

		if (cur_size == 0)
			break;

		if (extent_paddr != 0)
		{
			if (blk_offs == 0 && cur_size > m_blksize)
				cur_size &= m_blksize_mask_hi;

			if (blk_offs == 0 && (cur_size & m_blksize_mask_lo) == 0)
			{
				if (g_debug & Dbg_Dir)
					std::cout << "Full read blk " << extent_paddr + blk_idx << " cnt " << (cur_size >> m_blksize_sh) << std::endl;
				m_vol.ReadBlocks(bdata, extent_paddr + blk_idx, cur_size >> m_blksize_sh, extent_crypto_id + blk_idx);
			}
			else
			{
				if (g_debug & Dbg_Dir)
					std::cout << "Partial read blk " << extent_paddr + blk_idx << " cnt 1" << std::endl;

				m_vol.ReadBlocks(m_tmp_blk.data(), extent_paddr + blk_idx, 1, extent_crypto_id + blk_idx);

				if (blk_offs + cur_size > m_blksize)
					cur_size = m_blksize - blk_offs;

				if (g_debug & Dbg_Dir)
					std::cout << "Partial copy off " << blk_offs << " size " << cur_size << std::endl;

				memcpy(bdata, m_tmp_blk.data() + blk_offs, cur_size);
			}
		}
		else
			memset(bdata, 0, cur_size);
		bdata += cur_size;
		offs += cur_size;
		size -= cur_size;
		// printf("ReadFile: offs=%016lX size=%016lX\n", offs, size);
	}

	return true;
}

bool ApfsDir::ListAttributes(std::vector<std::string>& names, uint64_t inode)
{
	j_inode_key_t skey;
	const j_xattr_key_t *ekey;
	BTreeIterator it;
	BTreeEntry res;
	bool rc;

	skey.hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_INODE, inode);

	rc = m_fs_tree.GetIterator(it, &skey, sizeof(j_inode_key_t), CompareStdDirKey, this);
	if (!rc)
		return false;

	for (;;)
	{
		rc = it.GetEntry(res);
		if (!rc)
			break;

		ekey = reinterpret_cast<const j_xattr_key_t *>(res.key);

		if ((ekey->hdr.obj_id_and_type & OBJ_ID_MASK) != inode)
			break;

		if ((ekey->hdr.obj_id_and_type >> OBJ_TYPE_SHIFT) < APFS_TYPE_XATTR)
		{
			it.next();
			continue;
		}

		if ((ekey->hdr.obj_id_and_type >> OBJ_TYPE_SHIFT) > APFS_TYPE_XATTR)
			break;

		names.push_back(reinterpret_cast<const char *>(ekey->name));

		it.next();
	}

	return true;
}

bool ApfsDir::GetAttribute(std::vector<uint8_t>& data, uint64_t inode, const char* name)
{
	uint8_t skey_buf[0x500];
	j_xattr_key_t *skey = reinterpret_cast<j_xattr_key_t *>(skey_buf);
	const j_xattr_val_t *attr;
	const j_xattr_dstream_t *xstm = nullptr;

	// const uint8_t *adata;
	BTreeEntry res;
	bool rc;

	size_t name_len = strlen(name) + 1;
	if (name_len > 0x400)
		return false;

	skey->hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_XATTR, inode);
	skey->name_len = static_cast<int16_t>(name_len);
	memcpy(skey->name, name, skey->name_len);

	rc = m_fs_tree.Lookup(res, skey, sizeof(j_xattr_key_t) + skey->name_len, CompareStdDirKey, this, true);
	if (!rc)
		return false;

	attr = reinterpret_cast<const j_xattr_val_t *>(res.val);
	// adata = reinterpret_cast<const uint8_t *>(res.val) + sizeof(APFS_Xattr_Val);

	if (g_debug & Dbg_Dir)
		std::cout << "GetAttribute: type=" << attr->flags << std::endl;

	if (attr->flags & XATTR_DATA_STREAM)
	{
		// Attribute contents are stored in a file
		assert(attr->xdata_len == sizeof(j_xattr_dstream_t));
		xstm = reinterpret_cast<const j_xattr_dstream_t *>(attr->xdata);

		if (g_debug & Dbg_Dir)
		{
			std::cout << "Attribute is link:" << std::endl;
			std::cout << "  obj_id       : " << xstm->xattr_obj_id << std::endl;
			std::cout << "  size         : " << xstm->dstream.size << std::endl;
			std::cout << "  alloced_size : " << xstm->dstream.alloced_size << std::endl;
			std::cout << "  default_crypto_id : " << xstm->dstream.default_crypto_id << std::endl;
			std::cout << "  total_bytes_written  : " << xstm->dstream.total_bytes_written << std::endl;
			std::cout << "  total_bytes_read  : " << xstm->dstream.total_bytes_read << std::endl;
		}

		data.resize(xstm->dstream.alloced_size);
		ReadFile(data.data(), xstm->xattr_obj_id, 0, data.size()); // Read must be multiple of 4K ...
		data.resize(xstm->dstream.size);

		if (g_debug & Dbg_Dir)
		{
			size_t dmpsize = 0x40;
			if (dmpsize > data.size())
				dmpsize = data.size();
			DumpBuffer(data.data(), dmpsize, "start of attribute content");
		}
	}
	else if (attr->flags & XATTR_DATA_EMBEDDED)
	{
		// Attribute contents are stored in the btree
		data.assign(attr->xdata, attr->xdata + attr->xdata_len);
	}

	return true;
}

bool ApfsDir::GetAttributeInfo(ApfsDir::XAttr& attr, uint64_t inode, const char* name)
{
	uint8_t skey_buf[0x500];
	j_xattr_key_t *skey = reinterpret_cast<j_xattr_key_t *>(skey_buf);
	const j_xattr_val_t *xv;
	const j_xattr_dstream_t *xs = nullptr;

	BTreeEntry res;
	bool rc;

	size_t name_len = strlen(name) + 1;
	if (name_len > 0x400)
		return false;

	skey->hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_XATTR, inode);
	skey->name_len = static_cast<int16_t>(name_len);
	memcpy(skey->name, name, skey->name_len);

	rc = m_fs_tree.Lookup(res, skey, sizeof(j_xattr_key_t) + skey->name_len, CompareStdDirKey, this, true);
	if (!rc)
		return false;

	xv = reinterpret_cast<const j_xattr_val_t *>(res.val);
	attr.flags = xv->flags;
	attr.xdata_len = xv->xdata_len;

	if (xv->flags & XATTR_DATA_STREAM)
	{
		xs = reinterpret_cast<const j_xattr_dstream_t *>(xv->xdata);
		attr.xstrm = *xs;
	}

	return true;
}

int ApfsDir::CompareStdDirKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context)
{
	// assert(skey_len == 8);
	// assert(ekey_len == 8);

	(void)ekey_len;

	ApfsDir *dir = reinterpret_cast<ApfsDir *>(context);

	uint64_t ks = *reinterpret_cast<const le_uint64_t *>(skey);
	uint64_t ke = *reinterpret_cast<const le_uint64_t *>(ekey);

	// std::cout << std::hex << std::uppercase << std::setfill('0');

	ks = (ks << 4) | (ks >> 60);
	ke = (ke << 4) | (ke >> 60);

	// std::cout << std::setw(16) << ks << " : " << std::setw(16) << ke << std::endl;

	if (ke < ks)
		return -1;
	if (ke > ks)
		return 1;

	if (skey_len > 8)
	{
		switch (ks & 0xF)
		{
		case APFS_TYPE_DIR_REC:
			if (dir->m_txt_fmt & 9)
			{
				const j_drec_hashed_key_t *s = reinterpret_cast<const j_drec_hashed_key_t *>(skey);
				const j_drec_hashed_key_t *e = reinterpret_cast<const j_drec_hashed_key_t *>(ekey);

				/* TODO: Is this correct? Yes. TODO check _apfs_cstrncmp. */
				if ((e->name_len_and_hash & J_DREC_HASH_MASK) < (s->name_len_and_hash & J_DREC_HASH_MASK))
					return -1;
				if ((e->name_len_and_hash & J_DREC_HASH_MASK) > (s->name_len_and_hash & J_DREC_HASH_MASK))
					return 1;

#if 1
				return StrCmpUtf8NormalizedFolded(e->name, s->name, (dir->m_txt_fmt & APFS_INCOMPAT_CASE_INSENSITIVE) != 0);
#else
				// TODO: This is not case insensitive ...
				for (size_t k = 0; k < (e->hash & 0x3FF); k++)
				{
					if (e->name[k] < s->name[k])
						return -1;
					if (e->name[k] > s->name[k])
						return 1;
				}
#endif
			}
			else
			{
				const j_drec_key_t *s = reinterpret_cast<const j_drec_key_t *>(skey);
				const j_drec_key_t *e = reinterpret_cast<const j_drec_key_t *>(ekey);

				size_t cnt = std::min(e->name_len, s->name_len);

				for (size_t k = 0; k < cnt; k++)
				{
					if (e->name[k] < s->name[k])
						return -1;
					if (e->name[k] > s->name[k])
						return 1;
				}

				if (e->name_len < s->name_len)
					return -1;
				if (e->name_len > s->name_len)
					return 1;
			}
			break;
		case APFS_TYPE_FILE_EXTENT:
		{
			const j_file_extent_key_t *s = reinterpret_cast<const j_file_extent_key_t *>(skey);
			const j_file_extent_key_t *e = reinterpret_cast<const j_file_extent_key_t *>(ekey);

			assert(skey_len == sizeof(j_file_extent_key_t));
			assert(ekey_len == sizeof(j_file_extent_key_t));

			if (e->logical_addr < s->logical_addr)
				return -1;
			if (e->logical_addr > s->logical_addr)
				return 1;
		}
			break;
		case APFS_TYPE_XATTR:
		{
			const j_xattr_key_t *s = reinterpret_cast<const j_xattr_key_t *>(skey);
			const j_xattr_key_t *e = reinterpret_cast<const j_xattr_key_t *>(ekey);

			size_t cnt = std::max(e->name_len, s->name_len);

			for (size_t k = 0; k < cnt; k++)
			{
				if (e->name[k] < s->name[k])
					return -1;
				if (e->name[k] > s->name[k])
					return 1;
			}

			if (e->name_len < s->name_len)
				return -1;
			if (e->name_len > s->name_len)
				return 1;
		}
			break;
		case APFS_TYPE_FILE_INFO:
		{
			const j_file_info_key_t *s = reinterpret_cast<const j_file_info_key_t *>(skey);
			const j_file_info_key_t *e = reinterpret_cast<const j_file_info_key_t *>(ekey);

			if (e->info_and_lba < s->info_and_lba)
				return -1;
			if (e->info_and_lba > s->info_and_lba)
				return 1;

			break;
		}
		}
	}

	return 0;
}

int ApfsDir::CompareFextKey(const void* skey, size_t skey_len, const void* ekey, size_t ekey_len, void* context)
{
	const fext_tree_key_t *s = reinterpret_cast<const fext_tree_key_t *>(skey);
	const fext_tree_key_t *e = reinterpret_cast<const fext_tree_key_t *>(ekey);

	if (e->private_id < s->private_id)
		return -1;
	if (e->private_id > s->private_id)
		return 1;
	if (e->logical_addr < s->logical_addr)
		return -1;
	if (e->logical_addr > s->logical_addr)
		return 1;
	return 0;
}

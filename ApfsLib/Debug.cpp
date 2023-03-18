/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2023 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cinttypes>

#include "DiskStruct.h"
#include "Util.h"
#include "Debug.h"
#include "BTree.h"

void dbg_print_hex(const void* vdata, size_t size)
{
	size_t n;
	const uint8_t* data = reinterpret_cast<const uint8_t*>(vdata);
	for (n = 0; n < size; n++)
		printf("%02X ", data[n]);
	for (n = 0; n < size; n++) {
		if (data[n] >= 0x20 && data[n] < 0x7F)
			printf("%c", data[n]);
		else
			printf(".");
	}
}

void dbg_dump_hex(const void* data, size_t size, size_t lsize)
{
	size_t x;
	size_t y;
	uint8_t b;
	const uint8_t* bdata = reinterpret_cast<const uint8_t*>(data);

	for (y = 0; y < size; y += lsize) {
		printf("%08zX : ", y);
		for (x = 0; x < lsize && (x + y) < size; x++)
			printf("%02X ", bdata[y + x]);
		for (; x < lsize; x++)
			printf("   ");
		printf(" ");
		for (x = 0; x < lsize && (x + y) < size; x++) {
			if (bdata[y + x] >= 0x20 && bdata[y + x] < 0x7F)
				printf("%c", bdata[y + x]);
			else
				printf(".");
		}
		printf("\n");
	}
}

void dbg_dump_hex_nz(const void* data, size_t size, size_t lsize)
{
	const uint8_t* bdata = reinterpret_cast<const uint8_t*>(data);
	ptrdiff_t k;

	for (k = size - 1; k >= 0; k--)
		if (bdata[k] != 0) break;
	++k;
	k = (k + 7) & ~7;
	if (k == 0)
		printf("<ZERO>\n");
	else
		dbg_dump_hex(data, size, lsize);
}

void dbg_print_btkey_fs(const void* key, uint16_t key_len, bool hashed)
{
	const j_key_t* jk = reinterpret_cast<const j_key_t*>(key);

	log_debug("[FS] %" PRIx64 "/%" PRIx64, jk->obj_id_and_type >> OBJ_TYPE_SHIFT, jk->obj_id_and_type & OBJ_ID_MASK);

	switch (jk->obj_id_and_type >> OBJ_TYPE_SHIFT) {
	case APFS_TYPE_XATTR:
	{
		const j_xattr_key_t* k = reinterpret_cast<const j_xattr_key_t*>(key);
		log_debug(" %d %s", k->name_len, k->name);
		break;
	}
	case APFS_TYPE_SIBLING_LINK:
	{
		const j_sibling_key_t* k = reinterpret_cast<const j_sibling_key_t*>(key);
		log_debug(" %" PRIx64, k->sibling_id);
		break;
	}
	case APFS_TYPE_DIR_REC:
	{
		if (hashed) {
			const j_drec_hashed_key_t* k = reinterpret_cast<const j_drec_hashed_key_t*>(key);
			log_debug(" %08X %s", k->name_len_and_hash, k->name);
		} else {
			const j_drec_key_t* k = reinterpret_cast<const j_drec_key_t*>(key);
			log_debug(" %d %s", k->name_len, k->name);
		}
		break;
	}
	case APFS_TYPE_FILE_EXTENT:
	{
		const j_file_extent_key_t* k = reinterpret_cast<const j_file_extent_key_t*>(key);
		log_debug(" %" PRIx64, k->logical_addr);
		break;
	}
	case APFS_TYPE_SNAP_NAME:
	{
		const j_snap_name_key_t* k = reinterpret_cast<const j_snap_name_key_t*>(key);
		log_debug(" %d %s", k->name_len, k->name);
		break;
	}

	default:
		if (key_len != 8)
			log_debug(" [!!! NOT IMPLEMENTED !!!]");
		break;
	}
}

void dbg_print_btree_key_int(const void* key, uint16_t key_len, uint32_t tree_subtype, bool fs_hashed)
{
	switch (tree_subtype) {
	case OBJECT_TYPE_FSTREE:
	case OBJECT_TYPE_FEXT_TREE:
	case OBJECT_TYPE_SNAPMETATREE:
		dbg_print_btkey_fs(key, key_len, fs_hashed);
		break;
	case OBJECT_TYPE_OMAP:
	{
		const omap_key_t* ok = reinterpret_cast<const omap_key_t*>(key);
		log_debug("[OM] %" PRIx64 " %" PRIx64, ok->ok_oid, ok->ok_xid);
		break;
	}
	default:
		dbg_print_hex(key, key_len);
		break;
	}
}

void dbg_print_btree_val_fs(const void* val, uint16_t val_len, int ktype)
{
	switch (ktype) {
	case APFS_TYPE_SNAP_METADATA:
	{
		const j_snap_metadata_val_t *smv = reinterpret_cast<const j_snap_metadata_val_t*>(val);
		log_debug("eref_oid=%" PRIx64 " apsb_oid=%" PRIx64 " ... inum=%" PRIx64 " eref_t=%08X flags=%08X %s",
			smv->extentref_tree_oid, smv->sblock_oid, smv->inum, smv->extentref_tree_type, smv->flags, smv->name);
		break;
	}
	case APFS_TYPE_SNAP_NAME:
	{
		const j_snap_name_val_t* snv = reinterpret_cast<const j_snap_name_val_t*>(val);
		log_debug("%" PRIx64, snv->snap_xid);
		break;
	}
	default:
		log_debug("[FS UNIMP %d] ", ktype);
		dbg_print_hex(val, val_len);
		break;
	}
}

void dbg_print_btree_val_int(const void* val, uint16_t val_len, uint32_t tree_subtype, int ktype)
{
	switch (tree_subtype) {
	case OBJECT_TYPE_FSTREE:
	case OBJECT_TYPE_FEXT_TREE:
	case OBJECT_TYPE_SNAPMETATREE:
		dbg_print_btree_val_fs(val, val_len, ktype);
		break;
	case OBJECT_TYPE_OMAP:
	{
		const omap_val_t* ov = reinterpret_cast<const omap_val_t*>(val);
		log_debug("%x %x %" PRIx64, ov->ov_flags, ov->ov_size, ov->ov_paddr);
		break;
	}
	default:
		dbg_print_hex(val, val_len);
		break;
	}
}

void dbg_print_btree_key(const void* key, uint16_t key_len, uint32_t tree_subtype, bool fs_hashed)
{
	dbg_print_btree_key_int(key, key_len, tree_subtype, fs_hashed);
	log_debug("\n");
}

void dbg_print_btree_entry(const void* key, uint16_t key_len, const void* val, uint16_t val_len, uint32_t tree_subtype, bool fs_hashed)
{
	dbg_print_btree_key_int(key, key_len, tree_subtype, fs_hashed);
	log_debug(" => ");
	if (tree_subtype == OBJECT_TYPE_FSTREE || tree_subtype == OBJECT_TYPE_FEXT_TREE || tree_subtype == OBJECT_TYPE_SNAPMETATREE) {
		const j_key_t* jkey = reinterpret_cast<const j_key_t*>(key);
		dbg_print_btree_val_int(val, val_len, tree_subtype, jkey->obj_id_and_type >> OBJ_TYPE_SHIFT);
	} else {
		dbg_print_btree_val_int(val, val_len, tree_subtype, 0);
	}
	log_debug("\n");
}

void dbg_dump_btree(BTree& tree)
{
	uint8_t key_buf[JOBJ_MAX_KEY_SIZE];
	uint8_t val_buf[JOBJ_MAX_VALUE_SIZE];
	BTreeIterator it;
	int err;

	log_debug("BTree: key_count=%" PRIu64 "\n", tree.key_count());

	err = it.initFirst(&tree, key_buf, JOBJ_MAX_KEY_SIZE, val_buf, JOBJ_MAX_VALUE_SIZE);
	if (err) return;

	for (;;) {
		dbg_print_btree_entry(key_buf, it.key_len(), val_buf, it.val_len(), tree.tree_type(), true);
		if (!it.next()) break;
	}
}

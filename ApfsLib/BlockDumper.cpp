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

#include "BlockDumper.h"
#include "Decmpfs.h"
#include "Util.h"

#include <ctime>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cstring>

#undef DUMP_COMPRESSED

static const FlagDesc fd_obj_type[] = {
	{ OBJ_EPHEMERAL, "OBJ_EPHEMERAL" },
	{ OBJ_PHYSICAL, "OBJ_PHYSICAL" },
	{ OBJ_NOHEADER, "OBJ_NOHEADER" },
	{ OBJ_ENCRYPTED, "OBJ_ENCRYPTED" },
	{ OBJ_NONPERSISTENT, "OBJ_NONPERSISTENT" },
	{ 0, 0 }
};

static const FlagDesc fd_nx_feat[] = {
	{ NX_FEATURE_DEFRAG, "NX_FEATURE_DEFRAG" },
	{ NX_FEATURE_LCFD, "NX_FEATURE_LCFD" },
	{ 0, 0 }
};

static const FlagDesc fd_nx_rocompat[] = {
	{ 0, 0 }
};

static const FlagDesc fd_nx_incompat[] = {
	{ NX_INCOMPAT_VERSION1, "NX_INCOMPAT_VERSION1" },
	{ NX_INCOMPAT_VERSION2, "NX_INCOMPAT_VERSION2" },
	{ NX_INCOMPAT_FUSION, "NX_INCOMPAT_FUSION" },
	{ 0, 0 }
};

static const FlagDesc fd_nx_flags[] = {
	{ NX_RESERVED_1, "NX_RESERVED_1" },
	{ NX_RESERVED_2, "NX_RESERVED_2" },
	{ NX_CRYPTO_SW, "NX_CRYPTO_SW" },
	{ 0, 0 }
};

static const FlagDesc fd_cpm_flags[] = {
	{ CHECKPOINT_MAP_LAST, "CHECKPOINT_MAP_LAST" },
	{ 0, 0 }
};

static const FlagDesc fd_om_flags[] = {
	{ OMAP_MANUALLY_MANAGED, "OMAP_MANUALLY_MANAGED" },
	{ OMAP_ENCRYPTING, "OMAP_ENCRYPTING" },
	{ OMAP_DECRYPTING, "OMAP_DECRYPTING" },
	{ OMAP_KEYROLLING, "OMAP_KEYROLLING" },
	{ OMAP_CRYPTO_GENERATION, "OMAP_CRYPTO_GENERATION" },
	{ 0, 0 }
};

static const FlagDesc fd_ov_flags[] = {
	{ OMAP_VAL_DELETED, "OMAP_VAL_DELETED" },
	{ OMAP_VAL_SAVED, "OMAP_VAL_SAVED" },
	{ OMAP_VAL_ENCRYPTED, "OMAP_VAL_ENCRYPTED" },
	{ OMAP_VAL_NOHEADER, "OMAP_VAL_NOHEADER" },
	{ OMAP_VAL_CRYPTO_GENERATION, "OMAP_VAL_CRYPTO_GENERATION" },
	{ 0, 0 }
};

static const FlagDesc fd_oms_flags[] = {
	{ OMAP_SNAPSHOT_DELETED, "OMAP_SNAPSHOT_DELETED" },
	{ OMAP_SNAPSHOT_REVERTED, "OMAP_SNAPSHOT_REVERTED" },
	{ 0, 0 }
};

static const FlagDesc fd_apfs_fs_flags[] = {
	{ APFS_FS_UNENCRYPTED, "APFS_FS_UNENCRYPTED" },
	{ APFS_FS_RESERVED_2, "APFS_FS_RESERVED_2" },
	{ APFS_FS_RESERVED_4, "APFS_FS_RESERVED_4" },
	{ APFS_FS_ONEKEY, "APFS_FS_ONEKEY" },
	{ APFS_FS_SPILLEDOVER, "APFS_FS_SPILLEDOVER" },
	{ APFS_FS_RUN_SPILLOVER_CLEANER, "APFS_FS_RUN_SPILLOVER_CLEANER" },
	{ APFS_FS_ALWAYS_CHECK_EXTENTREF, "APFS_FS_ALWAYS_CHECK_EXTENTREF" },
	{ APFS_FS_RESERVED_80, "APFS_FS_RESERVED_80" },
	{ APFS_FS_RESERVED_100, "APFS_FS_RESERVED_100" },
	{ 0, 0 }
};

static const FlagDesc fd_apfs_features[] = {
	{ APFS_FEATURE_DEFRAG_PRERELEASE, "APFS_FEATURE_DEFRAG_PRERELEASE" },
	{ APFS_FEATURE_HARDLINK_MAP_RECORDS, "APFS_FEATURE_HARDLINK_MAP_RECORDS" },
	{ APFS_FEATURE_DEFRAG, "APFS_FEATURE_DEFRAG" },
	{ APFS_FEATURE_STRICTATIME, "APFS_FEATURE_STRICTATIME" },
	{ APFS_FEATURE_VOLGRP_SYSTEM_INO_SPACE, "APFS_FEATURE_VOLGRP_SYSTEM_INO_SPACE" },
	{ 0, 0 }
};

static const FlagDesc fd_apfs_rocompat[] = {
	{ 0, 0 }
};

static const FlagDesc fd_apfs_incompat[] = {
	{ APFS_INCOMPAT_CASE_INSENSITIVE, "APFS_INCOMPAT_CASE_INSENSITIVE" },
	{ APFS_INCOMPAT_DATALESS_SNAPS, "APFS_INCOMPAT_DATALESS_SNAPS" },
	{ APFS_INCOMPAT_ENC_ROLLED, "APFS_INCOMPAT_ENC_ROLLED" },
	{ APFS_INCOMPAT_NORMALIZATION_INSENSITIVE, "APFS_INCOMPAT_NORMALIZATION_INSENSITIVE" },
	{ APFS_INCOMPAT_INCOMPLETE_RESTORE, "APFS_INCOMPAT_INCOMPLETE_RESTORE" },
	{ APFS_INCOMPAT_SEALED_VOLUME, "APFS_INCOMPAT_SEALED_VOLUME" },
	{ APFS_INCOMPAT_RESERVED_40, "APFS_INCOMPAT_RESERVED_40" },
	{ 0, 0 }
};

static const FlagDesc fd_j_inode_flags[] = {
	{ INODE_IS_APFS_PRIVATE, "INODE_IS_APFS_PRIVATE" },
	{ INODE_MAINTAIN_DIR_STATS, "INODE_MAINTAIN_DIR_STATS" },
	{ INODE_DIR_STATS_ORIGIN, "INODE_DIR_STATS_ORIGIN" },
	{ INODE_PROT_CLASS_EXPLICIT, "INODE_PROT_CLASS_EXPLICIT" },
	{ INODE_WAS_CLONED, "INODE_WAS_CLONED" },
	{ INODE_FLAGS_UNUSED, "INODE_FLAGS_UNUSED" },
	{ INODE_HAS_SECURITY_EA, "INODE_HAS_SECURITY_EA" },
	{ INODE_BEING_TRUNCATED, "INODE_BEING_TRUNCATED" },
	{ INODE_HAS_FINDER_INFO, "INODE_HAS_FINDER_INFO" },
	{ INODE_IS_SPARSE, "INODE_IS_SPARSE" },
	{ INODE_WAS_EVER_CLONED, "INODE_WAS_EVER_CLONED" },
	{ INODE_ACTIVE_FILE_TRIMMED, "INODE_ACTIVE_FILE_TRIMMED" },
	{ INODE_PINNED_TO_MAIN, "INODE_PINNED_TO_MAIN" },
	{ INODE_PINNED_TO_TIER2, "INODE_PINNED_TO_TIER2" },
	{ INODE_HAS_RSRC_FORK, "INODE_HAS_RSRC_FORK" },
	{ INODE_NO_RSRC_FORK, "INODE_NO_RSRC_FORK" },
	{ INODE_ALLOCATION_SPILLEDOVER, "INODE_ALLOCATION_SPILLEDOVER" },
	{ INODE_FAST_PROMOTE, "INODE_FAST_PROMOTE" },
	{ INODE_HAS_UNCOMPRESSED_SIZE, "INODE_HAS_UNCOMPRESSED_SIZE" },
	{ INODE_IS_PURGEABLE, "INODE_IS_PURGEABLE" },
	{ INODE_WANTS_TO_BE_PURGEABLE, "INODE_WANTS_TO_BE_PURGEABLE" },
	{ INODE_IS_SYNC_ROOT, "INODE_IS_SYNC_ROOT" },
	{ INODE_SNAPSHOT_COW_EXEMPTION, "INODE_SNAPSHOT_COW_EXEMPTION" },
	{ 0, 0 }
};

static const FlagDesc fd_j_xattr_flags[] = {
	{ XATTR_DATA_STREAM, "XATTR_DATA_STREAM" },
	{ XATTR_DATA_EMBEDDED, "XATTR_DATA_EMBEDDED" },
	{ XATTR_FILE_SYSTEM_OWNED, "XATTR_FILE_SYSTEM_OWNED" },
	{ XATTR_RESERVED_8, "XATTR_RESERVED_8" },
	{ 0, 0 }
};

static const FlagDesc fd_j_inode_mode[] = {
	{ MODE_S_IFIFO, "S_IFIFO" },
	{ MODE_S_IFCHR, "S_IFCHR" },
	{ MODE_S_IFDIR, "S_IFDIR" },
	{ MODE_S_IFBLK, "S_IFBLK" },
	{ MODE_S_IFREG, "S_IFREG" },
	{ MODE_S_IFLNK, "S_IFLNK" },
	{ MODE_S_IFSOCK, "S_IFSOCK" },
	{ MODE_S_IFWHT, "S_IFWHT" },
	{ MODE_S_IFMT, "S_IFMT" },
	{ 0, 0 }
};

static const FlagDesc fd_j_inode_bsd_flags[] = {
	{ APFS_UF_NODUMP, "UF_NODUMP" },
	{ APFS_UF_IMMUTABLE, "UF_IMMUTABLE" },
	{ APFS_UF_APPEND, "UF_APPEND" },
	{ APFS_UF_OPAQUE, "UF_OPAQUE" },
	{ APFS_UF_NOUNLINK, "UF_NOUNLINK" },
	{ APFS_UF_COMPRESSED, "UF_COMPRESSED" },
	{ APFS_UF_TRACKED, "UF_TRACKED" },
	{ APFS_UF_DATAVAULT, "UF_DATAVAULT" },
	{ APFS_UF_HIDDEN, "UF_HIDDEN" },
	{ APFS_SF_ARCHIVED, "SF_ARCHIVED" },
	{ APFS_SF_IMMUTABLE, "SF_IMMUTABLE" },
	{ APFS_SF_APPEND, "SF_APPEND" },
	{ APFS_SF_RESTRICTED, "SF_RESTRICTED" },
	{ APFS_SF_NOUNLINK, "SF_NOUNLINK" },
	{ APFS_SF_SNAPSHOT, "SF_SNAPSHOT" },
	{ APFS_SF_FIRMLINK, "SF_FIRMLINK" },
	{ APFS_SF_DATALESS, "SF_DATALESS" },
	{ 0, 0 }
};

static const FlagDesc fd_j_drec_flags[] = {
	{ DT_UNKNOWN, "DT_UNKNOWN" },
	{ DT_FIFO, "DT_FIFO" },
	{ DT_CHR, "DT_CHR" },
	{ DT_DIR, "DT_DIR" },
	{ DT_BLK, "DT_BLK" },
	{ DT_REG, "DT_REG" },
	{ DT_LNK, "DT_LNK" },
	{ DT_SOCK, "DT_SOCK" },
	{ DT_WHT, "DT_WHT" },
	{ 0, 0 }
};

static const FlagDesc fd_x_flags[] = {
	{ XF_DATA_DEPENDENT, "XF_DATA_DEPENDENT" },
	{ XF_DO_NOT_COPY, "XF_DO_NOT_COPY" },
	{ XF_RESERVED_4, "XF_RESERVED_4" },
	{ XF_CHILDREN_INHERIT, "XF_CHILDREN_INHERIT" },
	{ XF_USER_FIELD, "XF_USER_FIELD" },
	{ XF_SYSTEM_FIELD, "XF_SYSTEM_FIELD" },
	{ XF_RESERVED_40, "XF_RESERVED_40" },
	{ XF_RESERVED_80, "XF_RESERVED_80" },
	{ 0, 0 }
};

static const FlagDesc fd_snap_meta_flags[] = {
	{ SNAP_META_PENDING_DATALESS, "SNAP_META_PENDING_DATALESS" },
	{ SNAP_META_MERGE_IN_PROGRESS, "SNAP_META_MERGE_IN_PROGRESS" },
	{ 0, 0 }
};

static const FlagDesc fd_bt_flags[] = {
	{ BTREE_UINT64_KEYS, "BTREE_UINT64_KEYS" },
	{ BTREE_SEQUENTIAL_INSERT, "BTREE_SEQUENTIAL_INSERT" },
	{ BTREE_ALLOW_GHOSTS, "BTREE_ALLOW_GHOSTS" },
	{ BTREE_EPHEMERAL, "BTREE_EPHEMERAL" },
	{ BTREE_PHYSICAL, "BTREE_PHYSICAL" },
	{ BTREE_NONPERSISTENT, "BTREE_NONPERSISTENT" },
	{ BTREE_KV_NONALIGNED, "BTREE_KV_NONALIGNED" },
	{ BTREE_HASHED, "BTREE_HASHED" },
	{ BTREE_NOHEADER, "BTREE_NOHEADER" },
	{ 0, 0 }
};

static const FlagDesc fd_btn_flags[] = {
	{ BTNODE_ROOT, "BTNODE_ROOT" },
	{ BTNODE_LEAF, "BTNODE_LEAF" },
	{ BTNODE_FIXED_KV_SIZE, "BTNODE_FIXED_KV_SIZE" },
	{ BTNODE_HASHED, "BTNODE_HASHED" },
	{ BTNODE_NOHEADER, "BTNODE_NOHEADER" },
	{ BTNODE_CHECK_KOFF_INVAL, "BTNODE_CHECK_KOFF_INVAL" },
	{ 0, 0 }
};

static const FlagDesc fd_mt_flags[] = {
	{ FUSION_MT_DIRTY, "FUSION_MT_DIRTY" },
	{ FUSION_MT_TENANT, "FUSION_MT_TENANT" },
	{ 0, 0 }
};

using namespace std;

BlockDumper::BlockDumper(std::ostream &os, size_t blocksize) :
	m_os(os)
{
	m_block = nullptr;
	m_blocksize = blocksize;
	m_text_flags = 0x08; // Standard: Case-sensitive
}

BlockDumper::~BlockDumper()
{
}

void BlockDumper::DumpNode(const uint8_t *block, uint64_t blk_nr)
{
	using namespace std;

	m_block = block;

	m_os << hex << uppercase << setfill('0');

	const obj_phys_t * const obj = reinterpret_cast<const obj_phys_t *>(m_block);

	if (IsEmptyBlock(m_block, m_blocksize))
	{
		m_os << setw(16) << blk_nr << " [Empty]" << endl;
		m_block = nullptr;
		return;
	}

	/*
	if (!VerifyBlock(m_block, m_blocksize))
	{
		m_os << setw(16) << blk_nr << ":" << endl;
		DumpBlockHex();
		return;
	}
	*/

	DumpNodeHeader(obj, blk_nr);

	switch (obj->o_type & OBJECT_TYPE_MASK)
	{
	case OBJECT_TYPE_NX_SUPERBLOCK: // NXSB Block
		DumpBlk_NXSB();
		break;
	case OBJECT_TYPE_BTREE: // BTree Root
	case OBJECT_TYPE_BTREE_NODE: // BTree Node
		DumpBTNode_0();
		break;
		// MTREE ?
	case OBJECT_TYPE_SPACEMAN:
		DumpBlk_SM();
		break;
	case OBJECT_TYPE_SPACEMAN_CAB:
		DumpBlk_CAB();
		break;
	case OBJECT_TYPE_SPACEMAN_CIB: // Bitmap Block List
		DumpBlk_CIB();
		break;
		/* 8 - A */
	case OBJECT_TYPE_OMAP: // Pointer to Header (?)
		DumpBlk_OM();
		break;
	case OBJECT_TYPE_CHECKPOINT_MAP: // Another Mapping
		DumpBlk_CPM();
		break;
	case OBJECT_TYPE_FS: // APSB Block
		DumpBlk_APSB();
		break;
		/* E - 10 */
	case OBJECT_TYPE_NX_REAPER:
		DumpBlk_NR();
		break;
	case OBJECT_TYPE_NX_REAP_LIST:
		DumpBlk_NRL();
		break;

	// case OBJECT_TYPE_OMAP_SNAPSHOT:

	case OBJECT_TYPE_EFI_JUMPSTART:
		DumpBlk_JSDR();
		break;

	// case OBJECT_TYPE_FUSION_MIDDLE_TREE:

	case OBJECT_TYPE_NX_FUSION_WBC:
		DumpBlk_WBC();
		break;

	case OBJECT_TYPE_NX_FUSION_WBC_LIST:
		DumpBlk_WBCL();
		break;

	case OBJECT_TYPE_ER_STATE:
		DumpBlk_ER();
		break;

	case OBJECT_TYPE_SNAP_META_EXT:
		DumpBlk_SnapMetaExt();
		break;

	case OBJECT_TYPE_INTEGRITY_META:
		DumpBlk_IntegrityMeta();
		break;

	// case OBJECT_TYPE_GBITMAP:

	// case OBJECT_TYPE_GBITMAP_TREE:
	// case OBJECT_TYPE_GBITMAP_BLOCK:

	case 0:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_Root);
		break;

	default:
		// assert(false);
		std::cerr << "!!! UNKNOWN NODE TYPE " << hex << setw(8) << obj->o_type << " in block " << setw(16) << blk_nr << " !!!" << endl;
		m_os << "!!! UNKNOWN NODE TYPE !!!" << endl;
		DumpBlockHex();
		break;
	}

	m_os << endl;
	m_os << "===========================================================================================================================" << endl;
	m_os << endl;

	m_block = nullptr;
}

void BlockDumper::DumpNodeHeader(const obj_phys_t *blk, uint64_t blk_nr)
{
	m_os << "[paddr]          | cksum            | oid              | xid              | type     | subtype  | description" << endl;
	m_os << "-----------------+------------------+------------------+------------------+----------+----------+-----------------------" << endl;

	m_os << setw(16) << blk_nr << " | ";
	m_os << setw(16) << hexstr(blk->o_cksum, MAX_CKSUM_SIZE) << " | ";
	m_os << setw(16) << blk->o_oid << " | ";
	m_os << setw(16) << blk->o_xid << " | ";
	m_os << setw(8) << blk->o_type << " | ";
	m_os << setw(8) << blk->o_subtype << " | ";
	m_os << GetNodeType(blk->o_type, blk->o_subtype) << endl;
	m_os << endl;
}

#undef BT_VERBOSE

void BlockDumper::DumpBTNode(DumpFunc func, uint16_t keys_size, uint16_t values_size)
{
	const btree_node_phys_t * const btn = reinterpret_cast<const btree_node_phys_t *>(m_block);

	const uint8_t *key_ptr = nullptr;
	const uint8_t *val_ptr = nullptr;
	size_t key_len = 0;
	size_t val_len = 0;

	uint16_t base;
	uint16_t end;
	size_t k;

#ifdef BT_VERBOSE
	// TODO -> nloc_t
	struct FreeListEntry
	{
		uint16_t next_offs;
		uint16_t free_size;
	};

	const FreeListEntry *fle;
	uint16_t flo;

	uint16_t tot_key_free = 0;
	uint16_t tot_val_free = 0;
#endif

	// DumpHex(m_block, 0x1000);

	DumpBTHeader();

	base = btn->btn_table_space.off + btn->btn_table_space.len + sizeof(btree_node_phys_t);
	if (btn->btn_flags & BTNODE_ROOT)
		end = static_cast<uint16_t>(m_blocksize - sizeof(btree_info_t));
	else
		end = static_cast<uint16_t>(m_blocksize);

#ifdef BT_VERBOSE
	if (btn->btn_key_free_list.off != BTOFF_INVALID)
	{
		m_os << "Key Free List Space (" << setw(4) << (base + btn->btn_key_free_list.off) << ") :" << endl;
		// DumpHex(m_block + base + bt->key_free_list_space_offset, bt->key_free_list_space_length);
		flo = btn->btn_key_free_list.off;
		while (flo != BTOFF_INVALID)
		{
			fle = reinterpret_cast<const FreeListEntry *>(m_block + base + flo);
			m_os << setw(4) << flo << ":" << setw(4) << fle->next_offs << "/" << setw(4) << fle->free_size << endl;
			DumpHex(m_block + base + flo, fle->free_size);
			flo = fle->next_offs;
			tot_key_free += fle->free_size;
		}
	}

	if (btn->btn_val_free_list.off != BTOFF_INVALID)
	{
		m_os << "Val Free List Space (" << setw(4) << (end - btn->btn_val_free_list.off) << ") :" << endl;
		// DumpHex(m_block + end - bt->val_free_list_space_offset, bt->val_free_list_space_length);
		flo = btn->btn_val_free_list.off;
		while (flo != BTOFF_INVALID)
		{
			fle = reinterpret_cast<const FreeListEntry *>(m_block + end - flo);
			m_os << setw(4) << flo << ":" << setw(4) << fle->next_offs << "/" << setw(4) << fle->free_size << endl;
			DumpHex(m_block + end - flo, fle->free_size);
			flo = fle->next_offs;
			tot_val_free += fle->free_size;
		}
	}

	m_os << "Free List: Tot Key = " << tot_key_free << ", Tot Val = " << tot_val_free << endl;
	if (tot_key_free != btn->btn_key_free_list.len)
		m_os << "[!!!]" << endl;
	if (tot_val_free != btn->btn_val_free_list.len)
		m_os << "[!!!]" << endl;
#endif

	if (btn->btn_flags & BTNODE_FIXED_KV_SIZE)
	{
		if (keys_size == 0 || values_size == 0)
		{
			m_os << "!!! UNKNOWN FIXED KEY / VALUE SIZE !!!" << endl << endl;

			if ((btn->btn_o.o_type & OBJECT_TYPE_MASK) == OBJECT_TYPE_BTREE)
				DumpBTreeInfo();

			return;
		}

		const kvoff_t * const entry = reinterpret_cast<const kvoff_t *>(m_block + (btn->btn_table_space.off + sizeof(btree_node_phys_t)));

		for (k = 0; k < btn->btn_nkeys; k++)
		{
#ifdef BT_VERBOSE
			m_os << endl << k << ": " << setw(4) << entry[k].k << " / " << setw(4) << entry[k].v << endl;
#endif

			assert(entry[k].k != BTOFF_INVALID);

			if (entry[k].k == BTOFF_INVALID)
				continue;

			key_ptr = m_block + base + entry[k].k;
			key_len = keys_size;
			val_ptr = (entry[k].v == BTOFF_INVALID) ? nullptr : m_block + end - entry[k].v;
			val_len = (btn->btn_flags & BTNODE_LEAF) ? values_size : sizeof(oid_t);

			(*this.*func)(key_ptr, key_len, val_ptr, val_len, !(btn->btn_flags & BTNODE_LEAF));
		}
	}
	else
	{
		const kvloc_t * const entry = reinterpret_cast<const kvloc_t *>(m_block + (btn->btn_table_space.off + sizeof(btree_node_phys_t)));

		for (k = 0; k < btn->btn_nkeys; k++)
		{
#ifdef BT_VERBOSE
			m_os << endl << k << ": " << setw(4) << entry[k].k.off << " L " << setw(4) << entry[k].k.len;
			m_os << " / " << setw(4) << entry[k].v.off << " / " << setw(4) << entry[k].v.len << endl;
#endif

			assert(entry[k].k.off != BTOFF_INVALID);

			if (entry[k].k.off == BTOFF_INVALID)
				continue;

			key_ptr = m_block + base + entry[k].k.off;
			key_len = entry[k].k.len;
			val_ptr = (entry[k].v.off == BTOFF_INVALID) ? nullptr : m_block + end - entry[k].v.off;
			val_len = entry[k].v.len;

			(*this.*func)(key_ptr, key_len, val_ptr, val_len, !(btn->btn_flags & BTNODE_LEAF));
		}
	}

	m_os << endl;

	if (btn->btn_flags & BTNODE_ROOT)
		DumpBTreeInfo();
}

void BlockDumper::DumpBTHeader(bool dump_offsets)
{
	const btree_node_phys_t *bt = reinterpret_cast<const btree_node_phys_t *>(m_block);

	m_os << "Flgs | Levl | Key Cnt  | Table Area  | Free Area   | Key Free L  | Val Free L" << endl;
	m_os << "-----+------+----------+-------------+-------------+-------------+------------" << endl;

	m_os << setw(4) << bt->btn_flags << " | ";
	m_os << setw(4) << bt->btn_level << " | ";
	m_os << setw(8) << bt->btn_nkeys << " | ";
	m_os << setw(4) << bt->btn_table_space.off << " L " << setw(4) << bt->btn_table_space.len << " | ";
	m_os << setw(4) << bt->btn_free_space.off << " L " << setw(4) << bt->btn_free_space.len << " | ";
	m_os << setw(4) << bt->btn_key_free_list.off << " L " << setw(4) << bt->btn_key_free_list.len << " | ";
	m_os << setw(4) << bt->btn_val_free_list.off << " L " << setw(4) << bt->btn_val_free_list.len;
	m_os << "  [" << flagstr(bt->btn_flags, fd_btn_flags) << "]" << endl;
	m_os << endl;

	if (dump_offsets)
	{
		m_os << "Index:  " << setw(4) << bt->btn_table_space.off << " L " << setw(4) << bt->btn_table_space.len;
		m_os << " [" << setw(4) << (0x38 + bt->btn_table_space.off) << " - " << setw(4) << (0x38 + bt->btn_table_space.off + bt->btn_table_space.len) << "]" << endl;
		m_os << "Free:   " << setw(4) << bt->btn_free_space.off << " L " << setw(4) << bt->btn_free_space.len;
		m_os << " [" << setw(4) << (0x38 + bt->btn_table_space.off + bt->btn_table_space.len + bt->btn_free_space.off) << " - " << setw(4) << (0x38 + bt->btn_table_space.off + bt->btn_table_space.len + bt->btn_free_space.off + bt->btn_free_space.len) << "]" << endl;
		m_os << "K Free: " << setw(4) << bt->btn_key_free_list.off << " L " << setw(4) << bt->btn_key_free_list.len << endl;
		m_os << "V Free: " << setw(4) << bt->btn_val_free_list.off << " L " << setw(4) << bt->btn_val_free_list.len << endl;
		m_os << endl;

		size_t cnt;
		size_t k;

		if (bt->btn_flags & BTNODE_FIXED_KV_SIZE)
		{
			const kvoff_t *e = reinterpret_cast<const kvoff_t *>(m_block + sizeof(btree_node_phys_t));

			cnt = bt->btn_table_space.len / 4;

			for (k = 0; k < bt->btn_nkeys; k++)
				m_os << setw(2) << k << " : " << setw(4) << e[k].k << " => " << setw(4) << e[k].v << endl;
			m_os << "-----------------" << endl;
			for (; k < cnt; k++)
			{
				if (e[k].k != 0 || e[k].v != 0)
				{
					m_os << setw(2) << k << " : " << setw(4) << e[k].k << " => " << setw(4) << e[k].v << endl;
				}
			}
		}
		else
		{
			const kvloc_t *e = reinterpret_cast<const kvloc_t *>(m_block + sizeof(btree_node_phys_t));

			cnt = bt->btn_table_space.len / 8;

			for (k = 0; k < bt->btn_nkeys; k++)
			{
				m_os << setw(2) << k << " : ";
				m_os << setw(4) << e[k].k.off << " L " << setw(4) << e[k].k.len << " => ";
				m_os << setw(4) << e[k].v.off << " L " << setw(4) << e[k].v.len << endl;
			}
			m_os << "-------------------------------" << endl;
			for (; k < cnt; k++)
			{
				if (e[k].k.off != 0 || e[k].k.len != 0 || e[k].v.off != 0 || e[k].v.len != 0)
				{
					m_os << setw(2) << k << " : ";
					m_os << setw(4) << e[k].k.off << " L " << setw(4) << e[k].k.len << " => ";
					m_os << setw(4) << e[k].v.off << " L " << setw(4) << e[k].v.len << endl;
				}
			}
		}

		m_os << endl;
	}
}

void BlockDumper::DumpBTreeInfo()
{
	const btree_info_t * const info = reinterpret_cast<const btree_info_t *>(m_block + (m_blocksize - sizeof(btree_info_t)));

	m_os << endl;
	m_os << "Flags    | Nodesize | Key Size | Val Size | Key Max  | Val Max  | Key Count        | Node Count " << endl;
	m_os << "---------+----------+----------+----------+----------+----------+------------------+-----------------" << endl;
	m_os << setw(8) << info->bt_fixed.bt_flags << " | ";
	m_os << setw(8) << info->bt_fixed.bt_node_size << " | ";
	m_os << setw(8) << info->bt_fixed.bt_key_size << " | ";
	m_os << setw(8) << info->bt_fixed.bt_val_size << " | ";
	m_os << setw(8) << info->bt_longest_key << " | ";
	m_os << setw(8) << info->bt_longest_val << " | ";
	m_os << setw(16) << info->bt_key_count << " | ";
	m_os << setw(16) << info->bt_node_count;
	m_os << "  [" << flagstr(info->bt_fixed.bt_flags, fd_bt_flags) << "]" << endl;
}

void BlockDumper::DumpBTEntry_APFS_Root(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	// TODO: Sauber implementieren ...

	uint64_t key;
	// uint16_t nlen;
	uint8_t type;

	static const char *typestr[16] = {
		"Any     ",
		"SnapMeta",
		"Extent  ",
		"Inode   ",
		"XAttr   ",
		"SibLnk  ",
		"DStmID  ",
		"Crypto  ",
		"FileExt ",
		"DirRec  ",
		"DirStats",
		"SnapName",
		"SibMap  ",
		"FileInfo",
		"Undef-14",
		"Undef-15"
	};

	if (key_len < 8)
	{
		m_os << "!!! KEY LENGTH TOO SHORT : " << key_len << endl;
		DumpBTEntry_Unk(key_ptr, key_len, val_ptr, val_len, index);
		return;
	}

	key = *reinterpret_cast<const uint64_t *>(key_ptr);
	type = key >> OBJ_TYPE_SHIFT;
	key &= OBJ_ID_MASK;

	m_os << typestr[type] << ' ';

	switch (type)
	{
	// 1 = SnapMetadata
	// 2 = PhysExtent
	case APFS_TYPE_INODE:
		assert(key_len == 8);
		m_os << key << " => ";

		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			assert(val_len >= sizeof(j_inode_val_t));

			// m_os << "[" << setw(4) << val_len << "] ";

			const j_inode_val_t *obj = reinterpret_cast<const j_inode_val_t *>(val_ptr);

			m_os << obj->parent_id << " ";
			m_os << obj->private_id << " ";
#if 0
			m_os << obj->birthtime << " ";
			m_os << obj->mtime << " ";
			m_os << obj->ctime << " ";
			m_os << obj->atime << " ";
#else
			m_os << "[TS] ";
#endif
			m_os << obj->internal_flags << " [" << flagstr(obj->internal_flags, fd_j_inode_flags) << "] ";
			m_os << obj->nchildren << " ";
			m_os << obj->default_protection_class << " ";
			m_os << obj->write_generation_counter << " ";
			m_os << obj->bsd_flags << " [" << flagstr(obj->bsd_flags, fd_j_inode_bsd_flags) << "] ";
			m_os << dec;
			m_os << obj->owner << " ";
			m_os << obj->group << " ";
			m_os << oct << obj->mode << hex << " ";
			m_os << obj->pad1 << " ";
			m_os << obj->uncompressed_size;

			if (val_len > sizeof(j_inode_val_t))
				Dump_XF(obj->xfields, val_len - sizeof(j_inode_val_t), false);

			m_os << "  [" << flagstr(obj->internal_flags, fd_j_inode_flags) << "]";

			m_os << endl;
		}

		break;

	case APFS_TYPE_XATTR:
	{
		assert(key_len >= sizeof(j_xattr_key_t));
		const j_xattr_key_t *xk = reinterpret_cast<const j_xattr_key_t *>(key_ptr);

		m_os << key << " ";
		m_os << '\'' << xk->name << '\'';
		m_os << " => ";

		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			const j_xattr_val_t *xv = reinterpret_cast<const j_xattr_val_t *>(val_ptr);

			assert(xv->xdata_len + 4U == val_len);

			m_os << xv->flags << " [" << flagstr(xv->flags, fd_j_xattr_flags) << "] " << xv->xdata_len;

			if ((xv->flags & XATTR_DATA_STREAM) && xv->xdata_len == sizeof(j_xattr_dstream_t))
			{
				const j_xattr_dstream_t *attrlnk = reinterpret_cast<const j_xattr_dstream_t *>(xv->xdata);

				m_os << " : " << attrlnk->xattr_obj_id << " " << attrlnk->dstream.size << " " << attrlnk->dstream.alloced_size << " ";
				m_os << attrlnk->dstream.default_crypto_id << " " << attrlnk->dstream.total_bytes_written << " " << attrlnk->dstream.total_bytes_read << endl;
			}
			else if (xv->flags & XATTR_DATA_EMBEDDED)
			{
				const char *attr_name = reinterpret_cast<const char *>(xk->name);

				if (!strcmp(attr_name, "com.apple.fs.symlink"))
					m_os << " : " << '\'' << xv->xdata << '\'' << endl;
				else if (!strcmp(attr_name, "com.apple.quarantine"))
				{
					// Limit length ...
					std::string str(reinterpret_cast<const char *>(xv->xdata), val_len - 4);
					m_os << " : '" << str << '\'' << endl;
				}
				else if (!strcmp(attr_name, "com.apple.decmpfs"))
				{
					if (xv->xdata_len >= sizeof(CompressionHeader))
					{
						const CompressionHeader *cmpf = reinterpret_cast<const CompressionHeader *>(xv->xdata);
						if (cmpf->signature == 0x636D7066)
							m_os << " : 'cmpf' " << cmpf->algo << ' ' << cmpf->size;
						else
							m_os << " : [!!! Compression Header Invalid !!!]";
#ifdef DUMP_COMPRESSED
						if (xv->xdata_len > sizeof(CompressionHeader))
						{
							m_os << endl;
							DumpHex(xv->xdata + sizeof(CompressionHeader), xv->xdata_len - sizeof(CompressionHeader));
						}
#else
						if (xv->xdata_len > sizeof(CompressionHeader))
							m_os << " ...";
						m_os << endl;
#endif
					}
				}
#ifndef DUMP_COMPRESSED
				else if (!strncmp(attr_name, "com.apple.metadata:", 19))
				{
					m_os << endl;
					// Not really interested in e-mail metadata ...
				}
#endif
				else
				{
					m_os << endl;
					DumpHex(xv->xdata, xv->xdata_len);
				}
			}
		}

		break;
	}

	case APFS_TYPE_SIBLING_LINK:
	{
		assert(key_len == 0x10);

		const j_sibling_key_t *k = reinterpret_cast<const j_sibling_key_t *>(key_ptr);

		m_os << key << " ";
		m_os << k->sibling_id << " => ";

		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			const j_sibling_val_t * const v = reinterpret_cast<const j_sibling_val_t *>(val_ptr);

			m_os << v->parent_id << " ";
			m_os << "'" << v->name << "'";
			m_os << endl;
		}
		break;
	}

	case APFS_TYPE_DSTREAM_ID:
		assert(key_len == 8);
		m_os << key << " => ";
		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			assert(val_len == sizeof(j_dstream_id_val_t));
			const j_dstream_id_val_t *v = reinterpret_cast<const j_dstream_id_val_t *>(val_ptr);

			m_os << v->refcnt << endl;
		}

		break;

	case APFS_TYPE_CRYPTO_STATE:
		assert(key_len == 8);

		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			const j_crypto_val_t *c = reinterpret_cast<const j_crypto_val_t *>(val_ptr);

			m_os << key << " => ";
			m_os << c->refcnt << " : ";
			m_os << c->state.major_version << " ";
			m_os << c->state.minor_version << " ";
			m_os << c->state.cpflags << " ";
			m_os << c->state.persistent_class << " ";
			m_os << c->state.key_os_version << " ";
			m_os << c->state.key_revision << " ";
			m_os << c->state.key_len << endl;

			if (val_len > sizeof(j_crypto_val_t))
				DumpHex(reinterpret_cast<const uint8_t *>(val_ptr) + sizeof(j_crypto_val_t), val_len - sizeof(j_crypto_val_t));
		}

		break;

	case APFS_TYPE_FILE_EXTENT:
	{
		assert(key_len == 16);
		const j_file_extent_key_t *k = reinterpret_cast<const j_file_extent_key_t *>(key_ptr);

		m_os << key << " " << k->logical_addr << " => ";

		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			assert(val_len == sizeof(j_file_extent_val_t));

			const j_file_extent_val_t *ext = reinterpret_cast<const j_file_extent_val_t *>(val_ptr);
			uint16_t flags = ext->len_and_flags >> J_FILE_EXTENT_FLAG_SHIFT;
			uint64_t length = ext->len_and_flags & J_FILE_EXTENT_LEN_MASK;
			if (flags != 0)
				m_os << flags << "/";
			m_os << length << " " << ext->phys_block_num << " " << ext->crypto_id << endl;
		}

		break;
	}

	case APFS_TYPE_DIR_REC:
	{
		assert(key_len >= 10);

		m_os << key << " ";

		if (m_text_flags & (APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_NORMALIZATION_INSENSITIVE))
		{
			const j_drec_hashed_key_t *k = reinterpret_cast<const j_drec_hashed_key_t *>(key_ptr);

			m_os << setw(8) << k->name_len_and_hash << " ";

#if 0 // Hash verify
			uint32_t hash_calc = HashFilename(k->name, k->name_len_and_hash & J_DREC_LEN_MASK, (m_text_flags & APFS_INCOMPAT_CASE_INSENSITIVE) != 0);
			m_os << "[" << setw(8) << hash_calc << "] ";

			if (hash_calc != k->name_len_and_hash)
			{
				cerr << hex << "Hash not matching at name " << key << " : stored " << k->name_len_and_hash << ", calc " << hash_calc << " '";
				cerr << k->name << "'" << endl;

				m_os << endl;
				DumpHex(key_data, key_len, key_len);
			}
#endif

			m_os << '\'' << k->name << '\'';
		}
		else
		{
			const j_drec_key_t *k = reinterpret_cast<const j_drec_key_t *>(key_ptr);

			m_os << '\'' << k->name << '\'';
		}
		m_os << " => ";

		if (index)
		{
			DumpBTIndex(val_ptr, val_len);
		}
		else
		{
			const j_drec_val_t *ptr = reinterpret_cast<const j_drec_val_t *>(val_ptr);
			m_os << ptr->file_id << " [" << tstamp(ptr->date_added) << "] " << ptr->flags;

			if (val_len > sizeof(j_drec_val_t))
				Dump_XF(ptr->xfields, val_len - sizeof(j_drec_val_t), true);

			m_os << "  [" << enumstr(ptr->flags & DREC_TYPE_MASK, fd_j_drec_flags) << "]";

			m_os << endl;
		}

#if 0
		DumpHex(key_data + 12, key_len - 12);
#endif

		break;
	}

	// TODO: A = DirSize
	// B = SnapName

	case APFS_TYPE_SIBLING_MAP:
		m_os << key << " => ";
		if (index)
			DumpBTIndex(val_ptr, val_len);
		else
			m_os << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
		break;

	case APFS_TYPE_FILE_INFO:
	{
		const j_file_info_key_t *k = reinterpret_cast<const j_file_info_key_t*>(key_ptr);

		m_os << key << " " << k->info_and_lba << " => ";
		if (index) {
			DumpBTIndex(val_ptr, val_len);
		} else {
			const j_file_info_val_t *v = reinterpret_cast<const j_file_info_val_t*>(val_ptr);
			m_os << v->dhash.hashed_len << " " << static_cast<unsigned>(v->dhash.hash_size) << " ";
			for (uint8_t k = 0; k < v->dhash.hash_size; k++)
				m_os << setw(2) << static_cast<unsigned>(v->dhash.hash[k]);
			m_os << endl;
		}

		break;
	}

	case APFS_TYPE_DIR_STATS:
		m_os << key << " => ";
		if (index)
			DumpBTIndex(val_ptr, val_len);
		else {
			const j_dir_stats_val_t *v = reinterpret_cast<const j_dir_stats_val_t*>(val_ptr);
			m_os << "num_children=" << v->num_children << " total_size=" << v->total_size << " chained_key=" << v->chained_key << " gen_count=" << v->gen_count << endl;
		}
		break;

	default:
		m_os << "KEY TYPE UNKNOWN" << endl;

		DumpBTEntry_Unk(key_ptr, key_len, val_ptr, val_len, index);
		break;
	}

	m_os << hex;
}

void BlockDumper::DumpBTEntry_OMap(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	(void)key_len;
	(void)val_len;

	const omap_key_t *ekey = reinterpret_cast<const omap_key_t *>(key_ptr);

	m_os << ekey->ok_oid << " ";
	m_os << ekey->ok_xid << " => ";

	if (index)
	{
		m_os << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
	}
	else
	{
		if (!val_ptr)
		{
			m_os << "(NULL)" << endl;
		}
		else
		{
			const omap_val_t *val = reinterpret_cast<const omap_val_t *>(val_ptr);

			m_os << val->ov_flags << " ";
			m_os << "[" << flagstr(val->ov_flags, fd_ov_flags) << "] ";
			m_os << val->ov_size << " ";
			m_os << val->ov_paddr << endl;
		}
	}
}

void BlockDumper::DumpBTEntry_APFS_ExtentRef(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	(void)key_len;
	(void)val_len;

	assert(key_len == sizeof(uint64_t));
	uint64_t key = *reinterpret_cast<const uint64_t *>(key_ptr);
	uint16_t type = key >> OBJ_TYPE_SHIFT;

	key &= OBJ_ID_MASK;

	if (type != APFS_TYPE_EXTENT)
	{
		m_os << "!!! type != APFS_TYPE_EXTENT !!!" << endl;
		DumpBTEntry_Unk(key_ptr, key_len, val_ptr, val_len, index);
		return;
	}

	m_os << "Extent " << key << " => ";

	if (index)
	{
		assert(val_len == sizeof(oid_t));
		m_os << *reinterpret_cast<const oid_t *>(val_ptr) << endl;
	}
	else
	{
		if (!val_ptr)
		{
			m_os << "(NULL)" << endl;
		}
		else
		{
			assert(val_len == sizeof(j_phys_ext_val_t));
			const j_phys_ext_val_t *val = reinterpret_cast<const j_phys_ext_val_t *>(val_ptr);
			uint64_t len;
			uint16_t kind;

			kind = val->len_and_kind >> PEXT_KIND_SHIFT;
			len = val->len_and_kind & PEXT_LEN_MASK;

			m_os << kind << "/" << len << " ";
			m_os << val->owning_obj_id << " ";
			m_os << val->refcnt << endl;
		}
	}
}

void BlockDumper::DumpBTEntry_APFS_SnapMeta(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	(void)key_len;
	(void)val_len;

	uint64_t key = *reinterpret_cast<const uint64_t *>(key_ptr);

	switch (key >> OBJ_TYPE_SHIFT)
	{
	case APFS_TYPE_SNAP_METADATA:
	{
		m_os << "SnapMeta " << key;
		break;
	}
	case APFS_TYPE_SNAP_NAME:
	{
		const j_snap_name_key_t *k = reinterpret_cast<const j_snap_name_key_t *>(key_ptr);
		m_os << "SnapName " << key << " '" << k->name << "'";
		break;
	}
	}

	m_os << " => ";

	if (!val_ptr)
	{
		m_os << "(NULL)" << endl;
	}
	else
	{
		if (index)
		{
			assert(val_len == sizeof(oid_t));
			m_os << setw(16) << *reinterpret_cast<const oid_t *>(val_ptr) << endl;
		}
		else
		{
			switch (key >> OBJ_TYPE_SHIFT)
			{
			case APFS_TYPE_SNAP_METADATA:
			{
				const j_snap_metadata_val_t *val = reinterpret_cast<const j_snap_metadata_val_t *>(val_ptr);
				m_os << setw(16) << val->extentref_tree_oid << " ";
				m_os << setw(16) << val->sblock_oid << " ";
				// m_os << setw(16) << val->change_time << " ";
				// m_os << setw(16) << val->create_time << " ";
				m_os << "[" << tstamp(val->change_time) << "] ";
				m_os << "[" << tstamp(val->create_time) << "] ";
				m_os << setw(16) << val->inum << " ";
				m_os << setw(8) << val->extentref_tree_type << " ";
				m_os << setw(8) << val->flags << " ";
				m_os << "'" << val->name << "'";
				break;
			}
			case APFS_TYPE_SNAP_NAME:
			{
				const j_snap_name_val_t *val = reinterpret_cast<const j_snap_name_val_t *>(val_ptr);
				m_os << setw(16) << val->snap_xid;
				break;
			}
			}
		}
	}
	m_os << endl;
}

void BlockDumper::DumpBTEntry_OMap_Snapshot(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	// OMAP-Snapshot ?

	(void)key_len;
	(void)val_len;
	(void)index;

	assert(key_len == sizeof(uint64_t));

	m_os << *reinterpret_cast<const uint64_t *>(key_ptr) << " => ";

	if (index)
	{
		assert(val_len == sizeof(oid_t));
		m_os << *reinterpret_cast<const oid_t *>(val_ptr);
	}
	else
	{
		assert(val_len == sizeof(omap_snapshot_t));
		const omap_snapshot_t *oms = reinterpret_cast<const omap_snapshot_t *>(val_ptr);

		m_os << oms->oms_flags << " [" << flagstr(oms->oms_flags, fd_oms_flags) << "] " << oms->oms_pad << " " << oms->oms_oid;
	}

	// DumpHex(key_ptr, 0x08);
	// DumpHex(val_ptr, 0x10);
	m_os << endl;
}

void BlockDumper::DumpBTEntry_FreeList(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	(void)key_len;
	(void)val_len;
	(void)index;

	const spaceman_free_queue_key_t *key = reinterpret_cast<const spaceman_free_queue_key_t *>(key_ptr);

	assert(key_len == sizeof(spaceman_free_queue_key_t));
	assert(val_len == sizeof(uint64_t));

	m_os << key->sfqk_xid << " ";
	m_os << key->sfqk_paddr << " => ";

	if (!val_ptr)
		m_os << "1/NULL" << endl;
	else
		m_os << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
}

void BlockDumper::DumpBTEntry_GBitmap(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	const uint64_t *k;
	const uint64_t *v;

	(void)key_len;
	(void)val_len;
	(void)index;

	assert(key_len == sizeof(uint64_t));
	assert(val_len == sizeof(uint64_t));

	k = reinterpret_cast<const uint64_t *>(key_ptr);
	v = reinterpret_cast<const uint64_t *>(val_ptr);

	m_os << *k << " => " << *v << std::endl;
}

void BlockDumper::DumpBTEntry_FusionMT(const void* key_ptr, size_t key_len, const void* val_ptr, size_t val_len, bool index)
{
	const fusion_mt_key_t *key = reinterpret_cast<const fusion_mt_key_t *>(key_ptr);

	m_os << key->paddr << " => ";

	if (index)
	{
		m_os << *reinterpret_cast<const le_oid_t *>(val_ptr) << endl;
	}
	else
	{
		const fusion_mt_val_t *val = reinterpret_cast<const fusion_mt_val_t *>(val_ptr);

		m_os << val->fmv_lba << ' ' << val->fmv_length << ' ' << val->fmv_flags << " [" << flagstr(val->fmv_flags, fd_mt_flags) << "]" << endl;
	}
}

void BlockDumper::DumpBTEntry_FExtTree(const void* key_ptr, size_t key_len, const void* val_ptr, size_t val_len, bool index)
{
	const fext_tree_key_t* key = reinterpret_cast<const fext_tree_key_t*>(key_ptr);

	m_os << key->private_id << ' ' << key->logical_addr << " => ";

	if (index)
		m_os << *reinterpret_cast<const le_oid_t*>(val_ptr) << endl;
	else {
		const fext_tree_val_t* val = reinterpret_cast<const fext_tree_val_t*>(val_ptr);
		m_os << val->len_and_flags << ' ' << val->phys_block_num << endl;
	}
}

void BlockDumper::DumpBTEntry_Unk(const void *key_ptr, size_t key_len, const void *val_ptr, size_t val_len, bool index)
{
	(void)index;

	m_os << "Key: " << std::endl;
	DumpHex(reinterpret_cast<const uint8_t *>(key_ptr), key_len);
	m_os << "Value: " << std::endl;
	DumpHex(reinterpret_cast<const uint8_t *>(val_ptr), val_len);
	m_os << std::endl;
}

void BlockDumper::DumpBTIndex(const void* val_ptr, uint16_t val_len)
{
	const btn_index_node_val_t *binv = reinterpret_cast<const btn_index_node_val_t*>(val_ptr);

	m_os << binv->binv_child_oid;
	if (val_len > 8) {
		m_os << " ";
		for (uint16_t n = 8; n < val_len; n++)
			m_os << setw(2) << static_cast<unsigned>(binv->binv_child_hash[n - 8]);
	}
	m_os << endl;
}

void BlockDumper::Dump_XF(const uint8_t * xf_data, size_t xf_size, bool drec)
{
	const xf_blob_t *h = reinterpret_cast<const xf_blob_t *>(xf_data);
	const x_field_t *e = reinterpret_cast<const x_field_t *>(h->xf_data);
	uint16_t entry_base = h->xf_num_exts * sizeof(x_field_t) + sizeof(xf_blob_t);
	uint16_t k;
	const uint8_t *data;

	if (xf_size < 4)
	{
		m_os << " [!!!XF size too small!!!]" << endl;
		return;
	}

	m_os << " XF: " << h->xf_num_exts << " " << h->xf_used_data << " : ";

	for (k = 0; k < h->xf_num_exts; k++)
	{
		m_os << setw(2) << static_cast<int>(e[k].x_type) << " ";
		m_os << setw(2) << static_cast<int>(e[k].x_flags) << " ";
		m_os << setw(4) << e[k].x_size << " : ";
	}

	for (k = 0; k < h->xf_num_exts; k++)
	{
		data = xf_data + entry_base;

		if (drec)
		{
			switch (e[k].x_type)
			{
			case DREC_EXT_TYPE_SIBLING_ID:
				m_os << "[SIB_ID] " << *reinterpret_cast<const uint64_t *>(data);
				break;
			default:
				m_os << "[!!!UNKNOWN!!!] ";
				DumpHex(data, e[k].x_size, e[k].x_size);
			}
		}
		else
		{
			switch (e[k].x_type)
			{
			case INO_EXT_TYPE_SNAP_XID:
				m_os << "[SNAP_XID] " << *reinterpret_cast<const xid_t *>(data);
				break;

			case INO_EXT_TYPE_DELTRA_TREE_OID:
				m_os << "[DELTA_TREE_OID] " << *reinterpret_cast<const oid_t *>(data);
				break;

			case INO_EXT_TYPE_DOCUMENT_ID:
				m_os << "[DOC_ID] " << *reinterpret_cast<const uint32_t *>(data);
				break;

			case INO_EXT_TYPE_NAME:
				m_os << "[NAME] '" << data << "'";
				break;

			case INO_EXT_TYPE_PREV_FSIZE:
				m_os << "[PREV_FSIZE] " << *reinterpret_cast<const uint64_t *>(data);
				break;

			case INO_EXT_TYPE_FINDER_INFO:
				m_os << "[FINDER_INFO] ... "; // TODO
				break;

			case INO_EXT_TYPE_DSTREAM:
			{
				const j_dstream_t *ft = reinterpret_cast<const j_dstream_t *>(data);
				m_os << "[DSTREAM] ";
				m_os << ft->size << " ";
				m_os << ft->alloced_size << " ";
				m_os << ft->default_crypto_id << " ";
				m_os << ft->total_bytes_written << " ";
				m_os << ft->total_bytes_read;
				break;
			}

			case INO_EXT_TYPE_DIR_STATS_KEY:
				m_os << "[DIR_STATS] !!! " << *reinterpret_cast<const uint64_t *>(data);
				break;

			case INO_EXT_TYPE_FS_UUID:
				m_os << "[FS_UUID] " << uuidstr(*reinterpret_cast<const apfs_uuid_t *>(data));
				break;

			case INO_EXT_TYPE_SPARSE_BYTES:
				m_os << "[SPARSE] " << *reinterpret_cast<const uint64_t *>(data);
				break;

			case INO_EXT_TYPE_RDEV:
				m_os << "[RDEV] " << *reinterpret_cast<const uint32_t *>(data);
				break;

			case INO_EXT_TYPE_PURGEABLE_FLAGS:
				m_os << "[PURGEABLE_FLAGS] " << *reinterpret_cast<const uint64_t *>(data);
				break;

			default:
				m_os << "[!!!UNKNOWN!!!] ";
				DumpHex(data, e[k].x_size, e[k].x_size);
			}

			entry_base += ((e[k].x_size + 7) & 0xFFF8);

			if (k < (h->xf_num_exts - 1))
				m_os << " : ";
		}
	}
}

void BlockDumper::DumpBlk_APSB()
{
	const apfs_superblock_t * const sb = reinterpret_cast<const apfs_superblock_t *>(m_block);
	int k;

	m_os << hex;
	m_os << "magic            : " << setw(8) << sb->apfs_magic << endl;
	m_os << "fs_index         : " << setw(8) << sb->apfs_fs_index << endl;
	m_os << "features         : " << setw(16) << sb->apfs_features << "  [" << flagstr(sb->apfs_features, fd_apfs_features) << "]" << endl;
	m_os << "ro_compat_feat   : " << setw(16) << sb->apfs_readonly_compatible_features << "  [" << flagstr(sb->apfs_readonly_compatible_features, fd_apfs_rocompat) << "]" << endl;
	m_os << "incompat_feat    : " << setw(16) << sb->apfs_incompatible_features << "  [" << flagstr(sb->apfs_incompatible_features, fd_apfs_incompat) << "]" << endl;
	m_os << "unmount_time     : " << tstamp(sb->apfs_unmount_time) << endl;
	m_os << "reserve_blk_cnt  : " << setw(16) << sb->apfs_fs_reserve_block_count << endl;
	m_os << "quota_blk_cnt    : " << setw(16) << sb->apfs_fs_quota_block_count << endl;
	m_os << "alloc_count      : " << setw(16) << sb->apfs_fs_alloc_count << endl;
	m_os << "  major_ver      : " << setw(4) << sb->apfs_meta_crypto.major_version << endl;
	m_os << "  minor_ver      : " << setw(4) << sb->apfs_meta_crypto.minor_version << endl;
	m_os << "  cpflags        : " << setw(8) << sb->apfs_meta_crypto.cpflags << endl;
	m_os << "  persistent_cls : " << setw(8) << sb->apfs_meta_crypto.persistent_class << endl;
	m_os << "  key_os_ver     : " << setw(8) << sb->apfs_meta_crypto.key_os_version << endl;
	m_os << "  key_os_rev     : " << setw(4) << sb->apfs_meta_crypto.key_revision << endl;
	m_os << "  unused         : " << setw(4) << sb->apfs_meta_crypto.unused << endl;
	m_os << "root_tree_type   : " << setw(8) << sb->apfs_root_tree_type << endl;
	m_os << "extentref_tree_t : " << setw(8) << sb->apfs_extentref_tree_type << endl;
	m_os << "snap_meta_tree_t : " << setw(8) << sb->apfs_snap_meta_tree_type << endl;
	m_os << "omap_oid         : " << setw(16) << sb->apfs_omap_oid << endl;
	m_os << "root_tree_oid    : " << setw(16) << sb->apfs_root_tree_oid << endl;
	m_os << "extentref_tree_o : " << setw(16) << sb->apfs_extentref_tree_oid << endl;
	m_os << "snap_meta_tree_o : " << setw(16) << sb->apfs_snap_meta_tree_oid << endl;
	m_os << "revert_to_xid    : " << setw(16) << sb->apfs_revert_to_xid << endl;
	m_os << "revert_to_sb_oid : " << setw(16) << sb->apfs_revert_to_sblock_oid << endl;
	m_os << "next_obj_id      : " << setw(16) << sb->apfs_next_obj_id << endl;
	m_os << "num_files        : " << setw(16) << sb->apfs_num_files << endl;
	m_os << "num_directories  : " << setw(16) << sb->apfs_num_directories << endl;
	m_os << "num_symlinks     : " << setw(16) << sb->apfs_num_symlinks << endl;
	m_os << "num_other_fsobjs : " << setw(16) << sb->apfs_num_other_fsobjects << endl;
	m_os << "num_snapshots    : " << setw(16) << sb->apfs_num_snapshots << endl;
	m_os << "total_blocks_alc : " << setw(16) << sb->apfs_total_blocks_alloced << endl;
	m_os << "total_blocks_frd : " << setw(16) << sb->apfs_total_blocks_freed << endl;
	m_os << "vol_uuid         : " << uuidstr(sb->apfs_vol_uuid) << endl;
	m_os << "last_mod_time    : " << tstamp(sb->apfs_last_mod_time) << endl;
	m_os << "fs_flags         : " << setw(16) << sb->apfs_fs_flags << "  [" << flagstr(sb->apfs_fs_flags, fd_apfs_fs_flags) << "]" << endl;
	m_os << "formatted_by id  : " << sb->apfs_formatted_by.id << endl;
	m_os << "    timestamp    : " << tstamp(sb->apfs_formatted_by.timestamp) << endl;
	m_os << "    last_xid     : " << setw(16) << sb->apfs_formatted_by.last_xid << endl;
	for (k = 0; k < 8; k++)
	{
		m_os << "modified_by id   : " << sb->apfs_modified_by[k].id << endl;
		m_os << "    timestamp    : " << tstamp(sb->apfs_modified_by[k].timestamp) << endl;
		m_os << "    last_xid     : " << setw(16) << sb->apfs_modified_by[k].last_xid << endl;
	}
	m_os << "volname          : " << sb->apfs_volname << endl;
	m_os << "next_doc_id      : " << setw(8) << sb->apfs_next_doc_id << endl;
	m_os << "role             : " << setw(4) << sb->apfs_role << endl;
	m_os << "reserved         : " << setw(4) << sb->reserved << endl;
	m_os << "root_to_xid      : " << setw(16) << sb->apfs_root_to_xid << endl;
	m_os << "er_state_oid     : " << setw(16) << sb->apfs_er_state_oid << endl;
	m_os << "cloneinfo_epoch  : " << setw(16) << sb->apfs_cloneinfo_id_epoch << endl;
	m_os << "cloneinfo_xid    : " << setw(16) << sb->apfs_cloneinfo_xid << endl;
	m_os << "snap_meta_ext_oid: " << setw(16) << sb->apfs_snap_meta_ext_oid << endl;
	m_os << "volume_group_id  : " << uuidstr(sb->apfs_volume_group_id) << endl;
	m_os << "integrity_meta_oi: " << setw(16) << sb->apfs_integrity_meta_oid << endl;
	m_os << "fext_tree_oid    : " << setw(16) << sb->apfs_fext_tree_oid << endl;
	m_os << "fext_tree_type   : " << setw(8) << sb->apfs_fext_tree_type << endl;
	m_os << "reserved_type    : " << setw(8) << sb->reserved_type << endl;
	m_os << "reserved_oid     : " << setw(16) << sb->reserved_oid << endl;

	m_os << endl;

	if (!IsZero(m_block + sizeof(apfs_superblock_t), m_blocksize - sizeof(apfs_superblock_t)))
	{
		m_os << "!!! ADDITIONAL DATA !!!" << endl;
		DumpBlockHex();
	}
}

void BlockDumper::DumpBlk_CAB()
{
	const cib_addr_block_t * const cab = reinterpret_cast<const cib_addr_block_t *>(m_block);

	size_t k;
	size_t cnt;

	dumpm("index     ", cab, cab->cab_index);
	dumpm("cib_count ", cab, cab->cab_cib_count);

	m_os << endl;

	cnt = cab->cab_cib_count;

	for (k = 0; k < cnt; k++)
		dumpm("cib_addr  ", cab, cab->cab_cib_addr[k]);
}

void BlockDumper::DumpBlk_CIB()
{
	const chunk_info_block_t * const cib = reinterpret_cast<const chunk_info_block_t *>(m_block);

	size_t k;

	dumpm("index      ", cib, cib->cib_index);
	dumpm("chunk_count", cib, cib->cib_chunk_info_count);
	m_os << endl;

	m_os << "Xid              | Offset           | Bits Tot | Bits Avl | Block" << endl;
	m_os << "-----------------+------------------+----------+----------+-----------------" << endl;

	for (k = 0; k < cib->cib_chunk_info_count; k++)
	{
		m_os << setw(16) << cib->cib_chunk_info[k].ci_xid << " | ";
		m_os << setw(16) << cib->cib_chunk_info[k].ci_addr << " | ";
		m_os << setw(8) << cib->cib_chunk_info[k].ci_block_count << " | ";
		m_os << setw(8) << cib->cib_chunk_info[k].ci_free_count << " | ";
		m_os << setw(16) << cib->cib_chunk_info[k].ci_bitmap_addr << endl;
	}
}

void BlockDumper::DumpBlk_OM()
{
	const omap_phys_t * const om = reinterpret_cast<const omap_phys_t *>(m_block);

	dumpm("flags          ", om, om->om_flags);
	dumpm("snap_count     ", om, om->om_snap_count);
	dumpm("tree_type      ", om, om->om_tree_type);
	dumpm("snap_tree_type ", om, om->om_snapshot_tree_type);
	dumpm("tree_oid       ", om, om->om_tree_oid);
	dumpm("snap_tree_oid  ", om, om->om_snapshot_tree_oid);
	dumpm("most_recent_snp", om, om->om_most_recent_snap);
	dumpm("pending_rev_min", om, om->om_pending_revert_min);
	dumpm("pending_rev_max", om, om->om_pending_revert_max);
	/* AA98B -> tree = AA9A6, unk_38=AAF85, unk_40=0650 */
	/* AAF85 -> oms btree */

	/*

	m_os << "Type 1   | Type 2   | Block" << endl;
	m_os << "---------+----------+-----------------" << endl;

	m_os << setw(8) << blk->entry[0].type_1 << " | ";
	m_os << setw(8) << blk->entry[0].type_2 << " | ";
	m_os << setw(16) << blk->entry[0].blk << endl;
	*/

	m_os << endl;

	if (!IsZero(m_block + sizeof(omap_phys_t), m_blocksize - sizeof(omap_phys_t)))
	{
		m_os << "!!! ADDITIONAL DATA !!!" << endl;
		DumpBlockHex();
	}
}

void BlockDumper::DumpBlk_CPM()
{
	const checkpoint_map_phys_t * const cpm = reinterpret_cast<const checkpoint_map_phys_t *>(m_block);

	size_t k;

	dumpm("cpm_flags", cpm, cpm->cpm_flags);
	dumpm("cpm_count", cpm, cpm->cpm_count);
	m_os << endl;

	m_os << "Type     | Subtype  | Size     | Pad      | FS-OID           | OID              | PAddr" << endl;
	m_os << "---------+----------+----------+----------+------------------+------------------+-----------------" << endl;

	for (k = 0; k < cpm->cpm_count; k++)
	{
		m_os << setw(8) << cpm->cpm_map[k].cpm_type << " | ";
		m_os << setw(8) << cpm->cpm_map[k].cpm_subtype << " | ";
		m_os << setw(8) << cpm->cpm_map[k].cpm_size << " | ";
		m_os << setw(8) << cpm->cpm_map[k].cpm_pad << " | ";
		m_os << setw(16) << cpm->cpm_map[k].cpm_fs_oid << " | ";
		m_os << setw(16) << cpm->cpm_map[k].cpm_oid << " | ";
		m_os << setw(16) << cpm->cpm_map[k].cpm_paddr << endl;
	}
}

void BlockDumper::DumpBlk_NXSB()
{
	const nx_superblock_t * const nx = reinterpret_cast<const nx_superblock_t *>(m_block);
	size_t k;

	m_os << hex;
	dumpm("magic           ", nx, nx->nx_magic);
	dumpm("block_size      ", nx, nx->nx_block_size);
	dumpm("block_count     ", nx, nx->nx_block_count);
	dumpm("features        ", nx, nx->nx_features, false);
	m_os << "  [" << flagstr(nx->nx_features, fd_nx_feat) << "]" << endl;
	dumpm("ro_compat_feat's", nx, nx->nx_readonly_compatible_features, false);
	m_os << "  [" << flagstr(nx->nx_readonly_compatible_features, fd_nx_rocompat) << "]" << endl;
	dumpm("incompat_feat's ", nx, nx->nx_incompatible_features, false);
	m_os << "  [" << flagstr(nx->nx_incompatible_features, fd_nx_incompat) << "]" << endl;
	dumpm("uuid            ", nx, nx->nx_uuid);
	dumpm("next_oid        ", nx, nx->nx_next_oid);
	dumpm("next_xid        ", nx, nx->nx_next_xid);
	dumpm("xp_desc_blocks  ", nx, nx->nx_xp_desc_blocks);
	dumpm("xp_data_blocks  ", nx, nx->nx_xp_data_blocks);
	dumpm("xp_desc_base    ", nx, nx->nx_xp_desc_base);
	dumpm("xp_data_base    ", nx, nx->nx_xp_data_base);
	dumpm("xp_desc_next    ", nx, nx->nx_xp_desc_next);
	dumpm("xp_data_next    ", nx, nx->nx_xp_data_next);
	dumpm("xp_desc_index   ", nx, nx->nx_xp_desc_index);
	dumpm("xp_desc_len     ", nx, nx->nx_xp_desc_len);
	dumpm("xp_data_index   ", nx, nx->nx_xp_data_index);
	dumpm("xp_data_len     ", nx, nx->nx_xp_data_len);
	dumpm("spaceman_oid    ", nx, nx->nx_spaceman_oid);
	dumpm("omap_oid        ", nx, nx->nx_omap_oid);
	dumpm("reaper_oid      ", nx, nx->nx_reaper_oid);
	dumpm("test_type       ", nx, nx->nx_test_type);
	dumpm("max_file_systems", nx, nx->nx_max_file_systems);
	m_os << endl;

	for (k = 0; k < nx->nx_max_file_systems; k++)
		if (nx->nx_fs_oid[k] != 0)
			dumpm("fs_oid          ", nx, nx->nx_fs_oid[k]);

	m_os << endl;

	for (k = 0; k < NX_NUM_COUNTERS; k++)
		dumpm("nx_counter      ", nx, nx->nx_counters[k]);

	dumpm("blocked_out_base", nx, nx->nx_blocked_out_prange.pr_start_addr);
	dumpm("blocked_out_blks", nx, nx->nx_blocked_out_prange.pr_block_count);
	dumpm("evict_map_tree  ", nx, nx->nx_evict_mapping_tree_oid);
	dumpm("flags           ", nx, nx->nx_flags, false);
	m_os << "  [" << flagstr(nx->nx_flags, fd_nx_flags) << "]" << endl;
	dumpm("efi_js_paddr    ", nx, nx->nx_efi_jumpstart);
	dumpm("fusion_uuid     ", nx, nx->nx_fusion_uuid);
	dumpm("keybag_base     ", nx, nx->nx_keylocker.pr_start_addr);
	dumpm("keybag_blocks   ", nx, nx->nx_keylocker.pr_block_count);
	dumpm("eph_info[0]     ", nx, nx->nx_ephemeral_info[0]);
	dumpm("eph_info[1]     ", nx, nx->nx_ephemeral_info[1]);
	dumpm("eph_info[2]     ", nx, nx->nx_ephemeral_info[2]);
	dumpm("eph_info[3]     ", nx, nx->nx_ephemeral_info[3]);
	dumpm("test_oid        ", nx, nx->nx_test_oid);
	dumpm("fusion_mt_oid   ", nx, nx->nx_fusion_mt_oid);
	dumpm("fusion_wbc_oid  ", nx, nx->nx_fusion_wbc_oid);
	dumpm("fusion_wbc.paddr", nx, nx->nx_fusion_wbc.pr_start_addr);
	dumpm("fusion_wbc.cnt  ", nx, nx->nx_fusion_wbc.pr_block_count);
	dumpm("newest_mounted_v", nx, nx->nx_newest_mounted_version);
	dumpm("mkb_locker.base ", nx, nx->nx_mkb_locker.pr_start_addr);
	dumpm("mkb_locker.count", nx, nx->nx_mkb_locker.pr_block_count);

	m_os << endl;

	if (!IsZero(m_block + sizeof(nx_superblock_t), m_blocksize - sizeof(nx_superblock_t)))
	{
		m_os << "!!! ADDITIONAL DATA !!!" << endl;
		DumpBlockHex();
	}

	DumpBlockHex();
}

void BlockDumper::DumpBlk_SM()
{
	size_t d;
	size_t k;
	size_t cnt;
	const spaceman_phys_t *b = reinterpret_cast<const spaceman_phys_t *>(m_block);

	static const char *devstr[2] = { "SD_MAIN", "SD_TIER2" };
	static const char *fqstr[3] = { "SFQ_IP", "SFQ_MAIN", "SFQ_TIER2" };

	m_os << hex;
	dumpm("block_size          ", b, b->sm_block_size);
	dumpm("blocks_per_chunk    ", b, b->sm_blocks_per_chunk);
	dumpm("chunks_per_cib      ", b, b->sm_chunks_per_cib);
	dumpm("cibs_per_cab        ", b, b->sm_cibs_per_cab);
	for (k = 0; k < SD_COUNT; k++)
	{
		m_os << "sm_dev[" << devstr[k] << "] {" << endl;
		dumpm("  block_count       ", b, b->sm_dev[k].sm_block_count);
		dumpm("  chunk_count       ", b, b->sm_dev[k].sm_chunk_count);
		dumpm("  cib_count         ", b, b->sm_dev[k].sm_cib_count);
		dumpm("  cab_count         ", b, b->sm_dev[k].sm_cab_count);
		dumpm("  free_count        ", b, b->sm_dev[k].sm_free_count);
		dumpm("  addr_offset       ", b, b->sm_dev[k].sm_addr_offset);
		dumpm("  reserved          ", b, b->sm_dev[k].sm_reserved);
		dumpm("  reserved2         ", b, b->sm_dev[k].sm_reserved2);
		m_os << "}" << endl;
	}
	dumpm("flags               ", b, b->sm_flags);
	dumpm("ip_bm_tx_multiplier ", b, b->sm_ip_bm_tx_multiplier);
	dumpm("ip_block_count      ", b, b->sm_ip_block_count);
	dumpm("ip_bm_size_in_blocks", b, b->sm_ip_bm_size_in_blocks);
	dumpm("ip_bm_block_count   ", b, b->sm_ip_bm_block_count);
	dumpm("ip_bm_base          ", b, b->sm_ip_bm_base);
	dumpm("ip_base             ", b, b->sm_ip_base);
	dumpm("fs_reserve_blk_cnt  ", b, b->sm_fs_reserve_block_count);
	dumpm("fs_reserve_alloc_cnt", b, b->sm_fs_reserve_alloc_count);
	for (k = 0; k < SFQ_COUNT; k++)
	{
		m_os << "sm_fq[" << fqstr[k] << "] {" << endl;
		dumpm("  count             ", b, b->sm_fq[k].sfq_count);
		dumpm("  tree_oid          ", b, b->sm_fq[k].sfq_tree_oid);
		dumpm("  oldest_xid        ", b, b->sm_fq[k].sfq_oldest_xid);
		dumpm("  tree_node_limit   ", b, b->sm_fq[k].sfq_tree_node_limit);
		dumpm("  pad16             ", b, b->sm_fq[k].sfq_pad16);
		dumpm("  pad32             ", b, b->sm_fq[k].sfq_pad32);
		dumpm("  reserved          ", b, b->sm_fq[k].sfq_reserved);
		m_os << "}" << endl;
	}

	dumpm("ip_bm_free_head     ", b, b->sm_ip_bm_free_head);
	dumpm("ip_bm_free_tail     ", b, b->sm_ip_bm_free_tail);
	dumpm("ip_bm_xid_offset    ", b, b->sm_ip_bm_xid_offset);
	dumpm("ip_bitmap_offset    ", b, b->sm_ip_bitmap_offset);
	dumpm("ip_bm_free_next_offs", b, b->sm_ip_bm_free_next_offset);
	dumpm("version             ", b, b->sm_version);
	dumpm("struct_size         ", b, b->sm_struct_size);
	m_os << endl;

	for (k = 0; k < SD_COUNT; k++) {
		for (d = 0; d < SM_DATAZONE_ALLOCZONE_COUNT; d++) {
			const spaceman_allocation_zone_info_phys_t &azip = b->sm_datazone.sdz_allocation_zones[k][d];
			m_os << "sdz_allocation_zones[" << k << "][" << d << "]:" << endl;
			m_os << "  saz_current_boundaries : " << setw(16) << azip.saz_current_boundaries.saz_zone_start << " " << setw(16) << azip.saz_current_boundaries.saz_zone_end << endl;
			for (int n = 0; n < SM_ALLOCZONE_NUM_PREVIOUS_BOUNDARIES; n++)
				m_os << "  saz_previous_boundaries: " << setw(16) << azip.saz_previous_boundaries[n].saz_zone_start << " " << setw(16) << azip.saz_previous_boundaries[n].saz_zone_end << endl;
			m_os << "  saz_zone_id            : " << setw(4) << azip.saz_zone_id << endl;
			m_os << "  saz_prev_boundary_idx  : " << setw(4) << azip.saz_previous_boundary_index << endl;
			m_os << "  saz_reserved           : " << setw(4) << azip.saz_reserved << endl;
		}
	}

	/*
	for (k = 0; k < 0x10; k++)
		dumpm("?                   ", b, b->unk_160[k]);
	m_os << endl;
	*/

	/*
	const le<uint16_t> *unk_arr = reinterpret_cast<const le<uint16_t> *>(m_block + b->sm_struct_size);
	for (k = 0; k < b->ip_bitmap_block_count; k++)
		dumpm("?                   ", b, unk_arr[k]);
		*/

	for (d = 0; d < SD_COUNT; d++)
	{
		m_os << "Device " << devstr[d] << " blocks:" << std::endl;
		if (b->sm_dev[d].sm_cab_count > 0)
			cnt = b->sm_dev[d].sm_cab_count;
		else
			cnt = b->sm_dev[d].sm_cib_count;

		if (b->sm_dev[d].sm_addr_offset != 0 && cnt != 0)
		{
			const le_uint64_t *addr = reinterpret_cast<const le_uint64_t *>(m_block + b->sm_dev[d].sm_addr_offset);

			for (k = 0; k < cnt; k++)
				dumpm("addr                ", b, addr[k]);
		}

		m_os << endl;
	}

#if 0
	if (b->unk_144 != 0)
	{
		const le_uint64_t &unk_xid = *reinterpret_cast<const le_uint64_t *>(m_block + b->unk_144);
		dumpm("unk_144->?          ", b, unk_xid);
	}

	if (b->unk_148 != 0)
	{
		const le_uint64_t &unk_148 = *reinterpret_cast<const le_uint64_t *>(m_block + b->unk_148);
		dumpm("unk_148->?          ", b, unk_148);
	}

	// unk_14C and unk_154 also point to some data ...

	/*

	m_os << "0180 BID BmpHdr  : " << setw(16) << b->blockid_vol_bitmap_hdr << endl;
	m_os << "..." << endl;
	m_os << "09D8 Some XID    : " << setw(16) << b->some_xid_9D8 << endl;
	m_os << "09E0             : " << setw(16) << b->unk_9E0 << endl;
	for (k = 0; k < 0x10; k++)
		m_os << setw(4) << (0x9E8 + 2 * k) << "             : " << setw(4) << b->unk_9E8[k] << endl;
	for (k = 0; k < 0xBF && b->bid_bmp_hdr_list[k] != 0; k++)
		m_os << setw(4) << (0xA08 + 8 * k) << "cib_oid      : " << setw(16) << b->bid_bmp_hdr_list[k] << endl;

	*/
#endif

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_NR()
{
	const nx_reaper_phys_t *nr = reinterpret_cast<const nx_reaper_phys_t *>(m_block);

	m_os << hex;

	dumpm("next_reap_id    ", nr, nr->nr_next_reap_id);
	dumpm("completed_id    ", nr, nr->nr_completed_id);
	dumpm("head            ", nr, nr->nr_head);
	dumpm("tail            ", nr, nr->nr_tail);
	dumpm("flags           ", nr, nr->nr_flags);
	dumpm("rlcount         ", nr, nr->nr_rlcount);
	dumpm("type            ", nr, nr->nr_type);
	dumpm("size            ", nr, nr->nr_size);
	dumpm("oid             ", nr, nr->nr_oid);
	dumpm("xid             ", nr, nr->nr_xid);
	dumpm("nrle_flags      ", nr, nr->nr_nrle_flags);
	dumpm("state_buf_size  ", nr, nr->nr_state_buffer_size);

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_NRL()
{
	const nx_reap_list_phys_t *nrl = reinterpret_cast<const nx_reap_list_phys_t *>(m_block);
	uint32_t k;

	m_os << hex;
	m_os << "next        : " << setw(16) << nrl->nrl_next << endl;
	m_os << "flags       : " << setw(8) << nrl->nrl_flags << endl;
	m_os << "max         : " << setw(8) << nrl->nrl_max << endl;
	m_os << "count       : " << setw(8) << nrl->nrl_count << endl;
	m_os << "first       : " << setw(8) << nrl->nrl_first << endl;
	m_os << "last        : " << setw(8) << nrl->nrl_last << endl;
	m_os << "free        : " << setw(8) << nrl->nrl_free << endl;
	m_os << endl;

	m_os << "next     | flags    | type     | size     | fs_oid           | oid              | xid" << endl;
	m_os << "---------+----------+----------+----------+------------------+------------------+-----------------" << endl;
	for (k = 0; k < nrl->nrl_max; k++)
	{
		m_os << setw(8) << nrl->nrl_entries[k].nrle_next << " | ";
		m_os << setw(8) << nrl->nrl_entries[k].nrle_flags << " | ";
		m_os << setw(8) << nrl->nrl_entries[k].nrle_type << " | ";
		m_os << setw(8) << nrl->nrl_entries[k].nrle_size << " | ";
		m_os << setw(16) << nrl->nrl_entries[k].nrle_fs_oid << " | ";
		m_os << setw(16) << nrl->nrl_entries[k].nrle_oid << " | ";
		m_os << setw(16) << nrl->nrl_entries[k].nrle_xid << endl;
	}

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_JSDR()
{
	const nx_efi_jumpstart_t *js = reinterpret_cast<const nx_efi_jumpstart_t *>(m_block);

	dumpm("magic           ", js, js->nej_magic);
	dumpm("version         ", js, js->nej_version);
	dumpm("efi_file_len    ", js, js->nej_efi_file_len);
	dumpm("num_extents     ", js, js->nej_num_extents);

	for (size_t k = 0; k < js->nej_num_extents; k++)
	{
		dumpm("apfs.efi base   ", js, js->nej_rec_extents[k].pr_start_addr);
		dumpm("apfs.efi blocks ", js, js->nej_rec_extents[k].pr_block_count);
	}

	m_os << endl;

	DumpBlockHex();
}

void BlockDumper::DumpBlk_ER()
{
	const er_state_phys_t *er = reinterpret_cast<const er_state_phys_t *>(m_block);

	dumpm("magic            ", er, er->ersb_header.ersb_magic);
	dumpm("version          ", er, er->ersb_header.ersb_version);

	if (er->ersb_header.ersb_version == 1)
	{
		const er_state_phys_v1_t *er1 = reinterpret_cast<const er_state_phys_v1_t *>(m_block);

		dumpm("flags            ", er1, er1->ersb_flags);
		dumpm("snap_xid         ", er1, er1->ersb_snap_xid);
		dumpm("cur_fext_obj_id  ", er1, er1->ersb_current_fext_obj_id);
		dumpm("file_offset      ", er1, er1->ersb_file_offset);
		dumpm("fext_pbn         ", er1, er1->ersb_fext_pbn);
		dumpm("paddr            ", er1, er1->ersb_paddr);
		dumpm("progress         ", er1, er1->ersb_progress);
		dumpm("total_blk_to_encr", er1, er1->ersb_total_blk_to_encrypt);
		dumpm("blockmap_oid     ", er1, er1->ersb_blockmap_oid);
		dumpm("checksum_count   ", er1, er1->ersb_checksum_count);
		dumpm("reserved         ", er1, er1->ersb_reserved);
		dumpm("fext_cid         ", er1, er1->ersb_fext_cid);
		m_os << "checksum : " << hexstr(er1->ersb_checksum, 8) << std::endl;
	}
	else
	{
		dumpm("flags            ", er, er->ersb_flags);
		dumpm("snap_xid         ", er, er->ersb_snap_xid);
		dumpm("cur_fext_obj_id  ", er, er->ersb_current_fext_obj_id);
		dumpm("file_offset      ", er, er->ersb_file_offset);
		dumpm("progress         ", er, er->ersb_progress);
		dumpm("total_blk_to_encr", er, er->ersb_total_blk_to_encrypt);
		dumpm("blockmap_oid     ", er, er->ersb_blockmap_oid);
		dumpm("tidemark_obj_id  ", er, er->ersb_tidemark_obj_id);
		dumpm("rec_extents_count", er, er->ersb_recovery_extents_count);
		dumpm("recovery_list_oid", er, er->ersb_recovery_list_oid);
		dumpm("recovery_length  ", er, er->ersb_recovery_length);
	}

	// dumpm("                 ", er, er->ersb_checksum);

	DumpBlockHex();
}

void BlockDumper::DumpBlk_WBC()
{
	const fusion_wbc_phys_t *wbc = reinterpret_cast<const fusion_wbc_phys_t *>(m_block);

	dumpm("version         ", wbc, wbc->fwp_version);
	dumpm("listHeadOid     ", wbc, wbc->fwp_listHeadOid);
	dumpm("listTailOid     ", wbc, wbc->fwp_listTailOid);
	dumpm("stableHeadOffset", wbc, wbc->fwp_stableHeadOffset);
	dumpm("stableTailOffset", wbc, wbc->fwp_stableTailOffset);
	dumpm("listBlocksCount ", wbc, wbc->fwp_listBlocksCount);
	dumpm("reserved        ", wbc, wbc->fwp_reserved);
	dumpm("usedByRC        ", wbc, wbc->fwp_usedByRC);
	dumpm("rcStash base    ", wbc, wbc->fwp_rcStash.pr_start_addr);
	dumpm("rcStash count   ", wbc, wbc->fwp_rcStash.pr_block_count);

	// DumpBlockHex();
}

void BlockDumper::DumpBlk_WBCL()
{
	const fusion_wbc_list_phys_t *wbcl = reinterpret_cast<const fusion_wbc_list_phys_t *>(m_block);
	uint32_t k;

	dumpm("version     ", wbcl, wbcl->fwlp_version);
	dumpm("tailOffset  ", wbcl, wbcl->fwlp_tailOffset);
	dumpm("indexBegin  ", wbcl, wbcl->fwlp_indexBegin);
	dumpm("indexEnd    ", wbcl, wbcl->fwlp_indexEnd);
	dumpm("indexMax    ", wbcl, wbcl->fwlp_indexMax);
	dumpm("reserved    ", wbcl, wbcl->fwlp_reserved);
	m_os << endl;
	m_os << "wbcLba           | targetLba        | length" << endl;
	m_os << "-----------------+------------------+-----------------" << endl;

	for (k = wbcl->fwlp_indexBegin; k < wbcl->fwlp_indexEnd; k++)
	{
		m_os << setw(16) << wbcl->fwlp_listEntries[k].fwle_wbcLba << " | ";
		m_os << setw(16) << wbcl->fwlp_listEntries[k].fwle_targetLba << " | ";
		m_os << setw(16) << wbcl->fwlp_listEntries[k].fwle_length << endl;
	}

	// DumpBlockHex();
}

void BlockDumper::DumpBlk_SnapMetaExt()
{
	const snap_meta_ext_obj_phys_t* sme = reinterpret_cast<const snap_meta_ext_obj_phys_t*>(m_block);

	dumpm("version     ", sme, sme->smeop_sme.sme_version);
	dumpm("flags       ", sme, sme->smeop_sme.sme_flags);
	dumpm("snap_xid    ", sme, sme->smeop_sme.sme_snap_xid);
	dumpm("uuid        ", sme, sme->smeop_sme.sme_uuid);
	dumpm("token       ", sme, sme->smeop_sme.sme_token);

	DumpBlockHex();
}

void BlockDumper::DumpBlk_IntegrityMeta()
{
	const integrity_meta_phys_t* const im = reinterpret_cast<const integrity_meta_phys_t*>(m_block);

	dumpm("version         ", im, im->im_version);
	dumpm("flags           ", im, im->im_flags);
	dumpm("hash_type       ", im, im->im_hash_type);
	dumpm("root_hash_offset", im, im->im_root_hash_offset);
	dumpm("broken_xid      ", im, im->im_broken_xid);

	DumpBlockHex();
}

void BlockDumper::DumpBTNode_0()
{
	const obj_phys_t * const hdr = reinterpret_cast<const obj_phys_t *>(m_block);

	switch (hdr->o_subtype)
	{
	case OBJECT_TYPE_SPACEMAN_FREE_QUEUE:
		DumpBTNode(&BlockDumper::DumpBTEntry_FreeList, 0x10, 0x08);
		break;

	case OBJECT_TYPE_OMAP:
		DumpBTNode(&BlockDumper::DumpBTEntry_OMap, 0x10, 0x10);
		break;

	case OBJECT_TYPE_FSTREE:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_Root);
		break;

	case OBJECT_TYPE_BLOCKREFTREE:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_ExtentRef);
		break;

	case OBJECT_TYPE_SNAPMETATREE:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_SnapMeta);
		break;

	case OBJECT_TYPE_OMAP_SNAPSHOT:
		DumpBTNode(&BlockDumper::DumpBTEntry_OMap_Snapshot, 0x8, 0x10);
		break;

	case OBJECT_TYPE_GBITMAP_TREE:
		DumpBTNode(&BlockDumper::DumpBTEntry_GBitmap, 8, 8);
		break;

	case OBJECT_TYPE_FUSION_MIDDLE_TREE:
		DumpBTNode(&BlockDumper::DumpBTEntry_FusionMT, 8, 16);
		break;

	case OBJECT_TYPE_FEXT_TREE:
		DumpBTNode(&BlockDumper::DumpBTEntry_FExtTree, 16, 16);
		break;

	default:
		DumpBTNode(&BlockDumper::DumpBTEntry_Unk);
		break;
	}
}

void BlockDumper::DumpBlockHex()
{
	unsigned int sz = 0xFFF;

	while (sz > 0 && m_block[sz] == 0)
		sz--;

	sz = (sz + 0x10) & 0xFFFFFFF0;

	::DumpHex(m_os, m_block, sz);
}

void BlockDumper::DumpHex(const uint8_t * data, size_t size, size_t line_size)
{
	::DumpHex(m_os, data, size, line_size);
}

std::string BlockDumper::flagstr(uint64_t flag, const FlagDesc *desc)
{
	size_t k;
	bool notfirst = false;
	std::string st;

	for (k = 0; desc[k].desc != 0; k++)
	{
		if (flag & desc[k].flag)
		{
			if (notfirst)
				st += ", ";
			st += desc[k].desc;
			notfirst = true;
		}
	}

	return st;
}

std::string BlockDumper::enumstr(uint64_t flag, const FlagDesc *desc)
{
	size_t k;
	std::string st;

	for (k = 0; desc[k].desc != 0; k++)
	{
		if (flag == desc[k].flag)
		{
			st = desc[k].desc;
			break;
		}
	}

	return st;
}

const char * BlockDumper::GetNodeType(uint32_t type, uint32_t subtype)
{
	const char *typestr = "Unknown";
	const char *names[0x21] = {
		"0",
		"Container Superblock",
		"B-Tree",
		"B-Tree Node",
		"M-Tree",
		"Spaceman",
		"Spaceman CIB Address Block",
		"Spaceman Chunk Info Block",
		"Spaceman Bitmap",
		"Spaceman Free Queue",
		"Extent List Tree",
		"Object Map",
		"Checkpoint Map",
		"Volume Superblock",
		"Filesystem Tree",
		"Block Ref Tree",
		"Snap Meta Tree",
		"NX Reaper",
		"NX Reap List",
		"OMap Snapshot",
		"EFI Jumpstart",
		"Fusion Middle Tree",
		"Fusion WBC",
		"Fusion WBC List",
		"ER State",
		"G Bitmap",
		"G Bitmap Tree",
		"G Bitmap Block",
		"ER Recovery Block",
		"Snap Meta Ext",
		"Integrity Meta",
		"Fext Tree",
		"Reserved 20"
	};

	int t = type & OBJECT_TYPE_MASK;

	if (t == OBJECT_TYPE_BTREE || t == OBJECT_TYPE_BTREE_NODE)
		t = subtype;

	if (t < 0x21)
		typestr = names[t];

	return typestr;
}

std::string BlockDumper::tstamp(uint64_t apfs_time)
{
	struct tm gmt;
	time_t secs;
	uint32_t nanos;
	std::stringstream st;

	nanos = apfs_time % 1000000000ULL;
	secs = apfs_time / 1000000000ULL;

#ifdef _MSC_VER
	gmtime_s(&gmt, &secs);
#else
	gmtime_r(&secs, &gmt);
	// Unix variant ...
#endif

	st << setfill('0');
	st << setw(4) << (gmt.tm_year + 1900) << "-";
	st << setw(2) << (gmt.tm_mon + 1) << "-";
	st << setw(2) << gmt.tm_mday << " ";
	st << setw(2) << gmt.tm_hour << ":";
	st << setw(2) << gmt.tm_min << ":";
	st << setw(2) << gmt.tm_sec << ".";
	st << setw(9) << nanos;

	return st.str();
}

#define DUMP_OT

void BlockDumper::dumpm(const char* name, const void *base, const uint8_t& v, bool lf)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u8  ";
#endif

	m_os << name << " : " << setw(2) << static_cast<unsigned>(v);
	if (lf)
		m_os << endl;
}

void BlockDumper::dumpm(const char* name, const void *base, const le_uint16_t& v, bool lf)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u16 ";
#endif

	m_os << name << " : " << setw(4) << v;
	if (lf)
		m_os << endl;
}

void BlockDumper::dumpm(const char* name, const void *base, const le_uint32_t& v, bool lf)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u32 ";
#endif

	m_os << name << " : " << setw(8) << v;
	if (lf)
		m_os << endl;
}

void BlockDumper::dumpm(const char* name, const void *base, const le_uint64_t& v, bool lf)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u64 ";
#endif

	m_os << name << " : " << setw(16) << v;
	if (lf)
		m_os << endl;
}

void BlockDumper::dumpm(const char* name, const void* base, const apfs_uuid_t& uuid)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&uuid) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " uid ";
#endif

	m_os << name << " : " << setw(16) << uuidstr(uuid) << endl;
}

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

#pragma once

#include <cstdint>

#include "ApfsTypes.h"
#include "Endian.h"

#pragma pack(push)
#pragma pack(1)

#ifdef _MSC_VER
#pragma warning(disable: 4200)
#endif

typedef le_uint64_t le_paddr_t;
typedef le_uint64_t le_oid_t;
typedef le_uint64_t le_xid_t;

struct prange_t {
	le_paddr_t pr_start_addr;
	le_uint64_t pr_block_count;
};

typedef uint32_t crypto_flags_t;
typedef uint32_t cp_key_class_t;
typedef uint32_t cp_key_os_version_t;
typedef uint16_t cp_key_revision_t;
typedef struct cpx* cpx_t;

typedef le_uint32_t le_crypto_flags_t;
typedef le_uint32_t le_cp_key_class_t;
typedef le_uint32_t le_cp_key_os_version_t;
typedef le_uint16_t le_cp_key_revision_t;

struct wrapped_crypto_state_t {
	le_uint16_t major_version;
	le_uint16_t minor_version;
	le_crypto_flags_t cpflags;
	le_cp_key_class_t persistent_class;
	le_cp_key_os_version_t key_os_version;
	le_cp_key_revision_t key_revision;
	le_uint16_t key_len;
	uint8_t persistent_key[0];
};

struct wrapped_meta_crypto_state_t {
	le_uint16_t major_version;
	le_uint16_t minor_version;
	le_crypto_flags_t cpflags;
	le_cp_key_class_t persistent_class;
	le_cp_key_os_version_t key_os_version;
	le_cp_key_revision_t key_revision;
	le_uint16_t unused;
};


constexpr size_t MAX_CKSUM_SIZE = 8;

struct obj_phys_t {
	uint8_t o_cksum[MAX_CKSUM_SIZE];
	le_oid_t o_oid;
	le_xid_t o_xid;
	le_uint32_t o_type;
	le_uint32_t o_subtype;
};

constexpr uint64_t OID_NX_SUPERBLOCK = 1;

constexpr uint64_t OID_INVALID = 0;
constexpr uint64_t OID_RESERVED_COUNT = 1024;

constexpr uint32_t OBJECT_TYPE_MASK = 0x0000FFFF;
constexpr uint32_t OBJECT_TYPE_FLAGS_MASK = 0xFFFF0000;

constexpr uint32_t OBJECT_TYPE_FLAGS_DEFINED_MASK = 0xF8000000;

constexpr uint32_t OBJECT_TYPE_NX_SUPERBLOCK = 0x00000001;

constexpr uint32_t OBJECT_TYPE_BTREE = 0x00000002;
constexpr uint32_t OBJECT_TYPE_BTREE_NODE = 0x00000003;

constexpr uint32_t OBJECT_TYPE_SPACEMAN = 0x00000005;
constexpr uint32_t OBJECT_TYPE_SPACEMAN_CAB = 0x00000006;
constexpr uint32_t OBJECT_TYPE_SPACEMAN_CIB = 0x00000007;
constexpr uint32_t OBJECT_TYPE_SPACEMAN_BITMAP = 0x00000008;
constexpr uint32_t OBJECT_TYPE_SPACEMAN_FREE_QUEUE = 0x00000009;

constexpr uint32_t OBJECT_TYPE_EXTENT_LIST_TREE = 0x0000000A;
constexpr uint32_t OBJECT_TYPE_OMAP = 0x0000000B;
constexpr uint32_t OBJECT_TYPE_CHECKPOINT_MAP = 0x0000000C;
constexpr uint32_t OBJECT_TYPE_FS = 0x0000000D;
constexpr uint32_t OBJECT_TYPE_FSTREE = 0x0000000E;
constexpr uint32_t OBJECT_TYPE_BLOCKREFTREE = 0x0000000F;
constexpr uint32_t OBJECT_TYPE_SNAPMETATREE = 0x00000010;

constexpr uint32_t OBJECT_TYPE_NX_REAPER = 0x00000011;
constexpr uint32_t OBJECT_TYPE_NX_REAP_LIST = 0x00000012;
constexpr uint32_t OBJECT_TYPE_OMAP_SNAPSHOT = 0x00000013;
constexpr uint32_t OBJECT_TYPE_EFI_JUMPSTART = 0x00000014;
constexpr uint32_t OBJECT_TYPE_FUSION_MIDDLE_TREE = 0x00000015;
constexpr uint32_t OBJECT_TYPE_NX_FUSION_WBC = 0x00000016;
constexpr uint32_t OBJECT_TYPE_NX_FUSION_WBC_LIST = 0x00000017;
constexpr uint32_t OBJECT_TYPE_ER_STATE = 0x00000018;

constexpr uint32_t OBJECT_TYPE_GBITMAP = 0x00000019;
constexpr uint32_t OBJECT_TYPE_GBITMAP_TREE = 0x0000001A;
constexpr uint32_t OBJECT_TYPE_GBITMAP_BLOCK = 0x0000001B;

constexpr uint32_t OBJECT_TYPE_ER_RECOVERY_BLOCK = 0x0000001C;
constexpr uint32_t OBJECT_TYPE_SNAP_META_EXT = 0x0000001D;
constexpr uint32_t OBJECT_TYPE_INTEGRITY_META = 0x0000001E;
constexpr uint32_t OBJECT_TYPE_FEXT_TREE = 0x0000001F;
constexpr uint32_t OBJECT_TYPE_RESERVED_20 = 0x00000020;

constexpr uint32_t OBJECT_TYPE_INVALID = 0;
constexpr uint32_t OBJECT_TYPE_TEST = 0x000000FF;

constexpr uint32_t OBJECT_TYPE_CONTAINER_KEYBAG = 0x7379656B; // 'keys'
constexpr uint32_t OBJECT_TYPE_VOLUME_KEYBAG = 0x73636572; // 'recs'
constexpr uint32_t OBJECT_TYPE_MEDIA_KEYBAG = 0x79656B6D; // 'mkey'

constexpr uint32_t OBJ_STORAGETYPE_MASK = 0xC0000000;

constexpr uint32_t OBJ_VIRTUAL = 0x00000000;
constexpr uint32_t OBJ_PHYSICAL = 0x40000000;
constexpr uint32_t OBJ_EPHEMERAL = 0x80000000;

constexpr uint32_t OBJ_NOHEADER = 0x20000000;
constexpr uint32_t OBJ_ENCRYPTED = 0x10000000;
constexpr uint32_t OBJ_NONPERSISTENT = 0x08000000;


constexpr uint32_t NX_EFI_JUMPSTART_MAGIC = 0x5244534A; // RDSJ
constexpr uint32_t NX_EFI_JUMPSTART_VERSION = 1;

struct nx_efi_jumpstart_t {
	obj_phys_t nej_o;
	le_uint32_t nej_magic;
	le_uint32_t nej_version;
	le_uint32_t nej_efi_file_len;
	le_uint32_t nej_num_extents;
	uint64_t nej_reserved[16];
	prange_t nej_rec_extents[];
};


constexpr uint32_t NX_MAGIC = 0x4253584E; // BSXN
constexpr int NX_MAX_FILE_SYSTEMS = 100;

constexpr int NX_EPH_INFO_COUNT = 4;
constexpr int NX_EPH_MIN_BLOCK_COUNT = 8;
constexpr int NX_MAX_FILE_SYSTEM_EPH_STRUCTS = 4;
constexpr int NX_TX_MIN_CHECKPOINT_COUNT = 4;
constexpr int NX_EPH_INFO_VERSION_1 = 1;

constexpr uint64_t NX_RESERVED_1 = 1;
constexpr uint64_t NX_RESERVED_2 = 2;
constexpr uint64_t NX_CRYPTO_SW = 4;

constexpr uint64_t NX_FEATURE_DEFRAG = 1;
constexpr uint64_t NX_FEATURE_LCFD = 2;
constexpr uint64_t NX_SUPPORTED_FEATURES_MASK = NX_FEATURE_DEFRAG | NX_FEATURE_LCFD;

constexpr uint64_t NX_SUPPORTED_ROCOMPAT_MASK = 0;

constexpr uint64_t NX_INCOMPAT_VERSION1 = 1;
constexpr uint64_t NX_INCOMPAT_VERSION2 = 2;
constexpr uint64_t NX_INCOMPAT_FUSION = 0x100;
constexpr uint64_t NX_SUPPORTED_INCOMPAT_MASK = NX_INCOMPAT_VERSION2 | NX_INCOMPAT_FUSION;

constexpr int NX_MINIMUM_BLOCK_SIZE = 4096;
constexpr int NX_DEFAULT_BLOCK_SIZE = 4096;
constexpr int NX_MAXIMUM_BLOCK_SIZE = 65536;

constexpr uint64_t NX_MINIMUM_CONTAINER_SIZE = 1048576;

enum nx_counter_id_t {
	NX_CNTR_OBJ_CKSUM_SET = 0,
	NX_CNTR_OBJ_CKSUM_FAIL = 1,

	NX_NUM_COUNTERS = 32
};

struct nx_superblock_t {
	obj_phys_t nx_o;
	le_uint32_t nx_magic;
	le_uint32_t nx_block_size;
	le_uint64_t nx_block_count;

	le_uint64_t nx_features;
	le_uint64_t nx_readonly_compatible_features;
	le_uint64_t nx_incompatible_features;

	apfs_uuid_t nx_uuid;

	le_oid_t nx_next_oid;
	le_xid_t nx_next_xid;

	le_uint32_t nx_xp_desc_blocks;
	le_uint32_t nx_xp_data_blocks;
	le_paddr_t nx_xp_desc_base;
	le_paddr_t nx_xp_data_base;
	le_uint32_t nx_xp_desc_next;
	le_uint32_t nx_xp_data_next;
	le_uint32_t nx_xp_desc_index;
	le_uint32_t nx_xp_desc_len;
	le_uint32_t nx_xp_data_index;
	le_uint32_t nx_xp_data_len;

	le_oid_t nx_spaceman_oid;
	le_oid_t nx_omap_oid;
	le_oid_t nx_reaper_oid;

	le_uint32_t nx_test_type;

	le_uint32_t nx_max_file_systems;
	le_oid_t nx_fs_oid[NX_MAX_FILE_SYSTEMS];
	le_uint64_t nx_counters[NX_NUM_COUNTERS];
	prange_t nx_blocked_out_prange;
	le_oid_t nx_evict_mapping_tree_oid;
	le_uint64_t nx_flags;
	le_paddr_t nx_efi_jumpstart;
	apfs_uuid_t nx_fusion_uuid;
	prange_t nx_keylocker;
	le_uint64_t nx_ephemeral_info[NX_EPH_INFO_COUNT];

	le_oid_t nx_test_oid;

	le_oid_t nx_fusion_mt_oid;
	le_oid_t nx_fusion_wbc_oid;
	prange_t nx_fusion_wbc;

	le_uint64_t nx_newest_mounted_version;

	prange_t nx_mkb_locker;
};


constexpr uint32_t CHECKPOINT_MAP_LAST = 0x00000001;


struct checkpoint_mapping_t {
	le_uint32_t cpm_type;
	le_uint32_t cpm_subtype;
	le_uint32_t cpm_size;
	le_uint32_t cpm_pad;
	le_oid_t cpm_fs_oid;
	le_oid_t cpm_oid;
	le_oid_t cpm_paddr;
};

struct checkpoint_map_phys_t {
	obj_phys_t cpm_o;
	le_uint32_t cpm_flags;
	le_uint32_t cpm_count;
	checkpoint_mapping_t cpm_map[];
};

struct evict_mapping_val_t {
	le_paddr_t dst_paddr;
	le_uint64_t len;
};


constexpr uint32_t OMAP_VAL_DELETED = 0x00000001;
constexpr uint32_t OMAP_VAL_SAVED = 0x00000002;
constexpr uint32_t OMAP_VAL_ENCRYPTED = 0x00000004;
constexpr uint32_t OMAP_VAL_NOHEADER = 0x00000008;
constexpr uint32_t OMAP_VAL_CRYPTO_GENERATION = 0x00000010;

constexpr uint32_t OMAP_SNAPSHOT_DELETED = 0x00000001;
constexpr uint32_t OMAP_SNAPSHOT_REVERTED = 0x00000002;

constexpr uint32_t OMAP_MANUALLY_MANAGED = 0x00000001;
constexpr uint32_t OMAP_ENCRYPTING = 0x00000002;
constexpr uint32_t OMAP_DECRYPTING = 0x00000004;
constexpr uint32_t OMAP_KEYROLLING = 0x00000008;
constexpr uint32_t OMAP_CRYPTO_GENERATION = 0x00000010;

constexpr uint32_t OMAP_VALID_FLAGS = 0x0000001F;

constexpr uint32_t OMAP_MAX_SNAP_COUNT = UINT32_MAX;

constexpr int OMAP_REAP_PHASE_MAP_TREE = 1;
constexpr int OMAP_REAP_PHASE_SNAPSHOT_TREE = 2;

struct omap_phys_t {
	obj_phys_t om_o;
	le_uint32_t om_flags;
	le_uint32_t om_snap_count;
	le_uint32_t om_tree_type;
	le_uint32_t om_snapshot_tree_type;
	le_oid_t om_tree_oid;
	le_oid_t om_snapshot_tree_oid;
	le_xid_t om_most_recent_snap;
	le_xid_t om_pending_revert_min;
	le_xid_t om_pending_revert_max;
};

struct omap_key_t {
	le_oid_t ok_oid;
	le_oid_t ok_xid;
};

struct omap_val_t {
	le_uint32_t ov_flags;
	le_uint32_t ov_size;
	le_paddr_t ov_paddr;
};

struct omap_snapshot_t {
	le_uint32_t oms_flags;
	le_uint32_t oms_pad;
	le_oid_t oms_oid;
};


constexpr uint32_t APFS_MAGIC = 0x42535041; // BSPA
constexpr int APFS_MAX_HIST = 8;
constexpr int APFS_VOLNAME_LEN = 256;


constexpr uint64_t APFS_FS_UNENCRYPTED = 0x01;
constexpr uint64_t APFS_FS_RESERVED_2 = 0x02; // APFS_FS_EFFACEABLE in older specs
constexpr uint64_t APFS_FS_RESERVED_4 = 0x04;
constexpr uint64_t APFS_FS_ONEKEY = 0x08;
constexpr uint64_t APFS_FS_SPILLEDOVER = 0x10;
constexpr uint64_t APFS_FS_RUN_SPILLOVER_CLEANER = 0x20;
constexpr uint64_t APFS_FS_ALWAYS_CHECK_EXTENTREF = 0x40;
constexpr uint64_t APFS_FS_RESERVED_80 = 0x80;
constexpr uint64_t APFS_FS_RESERVED_100 = 0x100;


constexpr uint64_t APFS_FS_FLAGS_VALID_MASK = APFS_FS_UNENCRYPTED | APFS_FS_RESERVED_2 | APFS_FS_RESERVED_4 | APFS_FS_ONEKEY
	| APFS_FS_SPILLEDOVER | APFS_FS_RUN_SPILLOVER_CLEANER | APFS_FS_ALWAYS_CHECK_EXTENTREF | APFS_FS_RESERVED_80 | APFS_FS_RESERVED_100;

constexpr uint64_t APFS_FS_CRYPTOFLAGS = APFS_FS_UNENCRYPTED | APFS_FS_ONEKEY;

constexpr int APFS_VOLUME_ENUM_SHIFT = 6;

constexpr uint16_t APFS_VOL_ROLE_NONE = 0x0000;

constexpr uint16_t APFS_VOL_ROLE_SYSTEM = 0x0001;
constexpr uint16_t APFS_VOL_ROLE_USER = 0x0002;
constexpr uint16_t APFS_VOL_ROLE_RECOVERY = 0x0004;
constexpr uint16_t APFS_VOL_ROLE_VM = 0x0008;

constexpr uint16_t APFS_VOL_ROLE_PREBOOT = 0x0010;
constexpr uint16_t APFS_VOL_ROLE_INSTALLER = 0x0020;
constexpr uint16_t APFS_VOL_ROLE_DATA = 1 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_BASEBAND = 2 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_UPDATE = 3 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_XART = 4 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_HARDWARE = 5 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_BACKUP = 6 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_RESERVED_7 = 7 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_RESERVED_8 = 8 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_ENTERPRISE = 9 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_RESERVED_10 = 10 << APFS_VOLUME_ENUM_SHIFT;
constexpr uint16_t APFS_VOL_ROLE_PRELOGIN = 11 << APFS_VOLUME_ENUM_SHIFT;

constexpr uint16_t APFS_VOL_ROLE_RESERVED_200 = 0x0200;


constexpr uint64_t APFS_FEATURE_DEFRAG_PRERELEASE = 1;
constexpr uint64_t APFS_FEATURE_HARDLINK_MAP_RECORDS = 2;
constexpr uint64_t APFS_FEATURE_DEFRAG = 4;
constexpr uint64_t APFS_FEATURE_STRICTATIME = 8;
constexpr uint64_t APFS_FEATURE_VOLGRP_SYSTEM_INO_SPACE = 0x10;

constexpr uint64_t APFS_SUPPORTED_FEATURES_MASK = APFS_FEATURE_DEFRAG | APFS_FEATURE_DEFRAG_PRERELEASE | APFS_FEATURE_HARDLINK_MAP_RECORDS | APFS_FEATURE_STRICTATIME | APFS_FEATURE_VOLGRP_SYSTEM_INO_SPACE;


constexpr uint64_t APFS_SUPPORTED_ROCOMPAT_MASK = 0;


constexpr uint64_t APFS_INCOMPAT_CASE_INSENSITIVE = 1;
constexpr uint64_t APFS_INCOMPAT_DATALESS_SNAPS = 2;
constexpr uint64_t APFS_INCOMPAT_ENC_ROLLED = 4;
constexpr uint64_t APFS_INCOMPAT_NORMALIZATION_INSENSITIVE = 8;
constexpr uint64_t APFS_INCOMPAT_INCOMPLETE_RESTORE = 0x10;
constexpr uint64_t APFS_INCOMPAT_SEALED_VOLUME = 0x20;
constexpr uint64_t APFS_INCOMPAT_RESERVED_40 = 0x40;

constexpr uint64_t APFS_SUPPORTED_INCOMPAT_MASK = APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_DATALESS_SNAPS | APFS_INCOMPAT_ENC_ROLLED
	| APFS_INCOMPAT_NORMALIZATION_INSENSITIVE | APFS_INCOMPAT_INCOMPLETE_RESTORE | APFS_INCOMPAT_SEALED_VOLUME | APFS_INCOMPAT_RESERVED_40;


constexpr int APFS_MODIFIED_NAMELEN = 32;

struct apfs_modified_by_t {
	uint8_t id[APFS_MODIFIED_NAMELEN];
	le_uint64_t timestamp;
	le_xid_t last_xid;
};

struct apfs_superblock_t {
	obj_phys_t apfs_o;

	le_uint32_t apfs_magic;
	le_uint32_t apfs_fs_index;

	le_uint64_t apfs_features;
	le_uint64_t apfs_readonly_compatible_features;
	le_uint64_t apfs_incompatible_features;

	le_uint64_t apfs_unmount_time;

	le_uint64_t apfs_fs_reserve_block_count;
	le_uint64_t apfs_fs_quota_block_count;
	le_uint64_t apfs_fs_alloc_count;

	wrapped_meta_crypto_state_t apfs_meta_crypto;

	le_uint32_t apfs_root_tree_type;
	le_uint32_t apfs_extentref_tree_type;
	le_uint32_t apfs_snap_meta_tree_type;

	le_oid_t apfs_omap_oid;
	le_oid_t apfs_root_tree_oid;
	le_oid_t apfs_extentref_tree_oid;
	le_oid_t apfs_snap_meta_tree_oid;

	le_xid_t apfs_revert_to_xid;
	le_oid_t apfs_revert_to_sblock_oid;

	le_uint64_t apfs_next_obj_id;

	le_uint64_t apfs_num_files;
	le_uint64_t apfs_num_directories;
	le_uint64_t apfs_num_symlinks;
	le_uint64_t apfs_num_other_fsobjects;
	le_uint64_t apfs_num_snapshots;

	le_uint64_t apfs_total_blocks_alloced;
	le_uint64_t apfs_total_blocks_freed;

	apfs_uuid_t apfs_vol_uuid;
	le_uint64_t apfs_last_mod_time;

	le_uint64_t apfs_fs_flags;

	apfs_modified_by_t apfs_formatted_by;
	apfs_modified_by_t apfs_modified_by[APFS_MAX_HIST];

	uint8_t apfs_volname[APFS_VOLNAME_LEN];
	le_uint32_t apfs_next_doc_id;

	le_uint16_t apfs_role;
	le_uint16_t reserved;

	le_xid_t apfs_root_to_xid;
	le_oid_t apfs_er_state_oid;

	le_uint64_t apfs_cloneinfo_id_epoch;
	le_uint64_t apfs_cloneinfo_xid;

	le_oid_t apfs_snap_meta_ext_oid;

	apfs_uuid_t apfs_volume_group_id;

	le_oid_t apfs_integrity_meta_oid;

	le_oid_t apfs_fext_tree_oid;
	le_uint32_t apfs_fext_tree_type;

	le_uint32_t reserved_type;
	le_oid_t reserved_oid;
};



struct j_key_t {
	le_uint64_t obj_id_and_type;
};

constexpr uint64_t OBJ_ID_MASK = 0x0FFFFFFFFFFFFFFFULL;
constexpr uint64_t OBJ_TYPE_MASK = 0xF000000000000000ULL;
constexpr int OBJ_TYPE_SHIFT = 60;


struct j_inode_key_t {
	j_key_t hdr;
};

struct j_inode_val_t {
	le_uint64_t parent_id;
	le_uint64_t private_id;

	le_uint64_t create_time;
	le_uint64_t mod_time;
	le_uint64_t change_time;
	le_uint64_t access_time;

	le_uint64_t internal_flags;

	union {
		le_int32_t nchildren;
		le_int32_t nlink;
	};

	le_cp_key_class_t default_protection_class;

	le_uint32_t write_generation_counter;
	le_uint32_t bsd_flags;
	le_uint32_t owner;
	le_uint32_t group;
	le_uint16_t mode;
	le_uint16_t pad1;
	le_uint64_t uncompressed_size;
	uint8_t xfields[];
};


constexpr uint32_t J_DREC_LEN_MASK = 0x000003FF;
constexpr uint32_t J_DREC_HASH_MASK = 0xFFFFFC00;
constexpr int J_DREC_HASH_SHIFT = 10;

struct j_drec_key_t {
	j_key_t hdr;
	le_uint16_t name_len;
	uint8_t name[0];
};

struct j_drec_hashed_key_t {
	j_key_t hdr;
	le_uint32_t name_len_and_hash;
	uint8_t name[0];
};

struct j_drec_val_t {
	le_uint64_t file_id;
	le_uint64_t date_added;
	le_uint16_t flags;
	uint8_t xfields[];
};


struct j_dir_stats_key_t {
	j_key_t hdr;
};

struct j_dir_stats_val_t {
	le_uint64_t num_children;
	le_uint64_t total_size;
	le_uint64_t chained_key;
	le_uint64_t gen_count;
};


struct j_xattr_key_t {
	j_key_t hdr;
	le_uint16_t name_len;
	uint8_t name[0];
};

struct j_xattr_val_t {
	le_uint16_t flags;
	le_uint16_t xdata_len;
	uint8_t xdata[0];
};


enum j_obj_types {
	APFS_TYPE_ANY = 0,

	APFS_TYPE_SNAP_METADATA = 1,
	APFS_TYPE_EXTENT = 2,
	APFS_TYPE_INODE = 3,
	APFS_TYPE_XATTR = 4,
	APFS_TYPE_SIBLING_LINK = 5,
	APFS_TYPE_DSTREAM_ID = 6,
	APFS_TYPE_CRYPTO_STATE = 7,
	APFS_TYPE_FILE_EXTENT = 8,
	APFS_TYPE_DIR_REC = 9,
	APFS_TYPE_DIR_STATS = 10,
	APFS_TYPE_SNAP_NAME = 11,
	APFS_TYPE_SIBLING_MAP = 12,
	APFS_TYPE_FILE_INFO = 13,

	APFS_TYPE_MAX_VALID = 13,
	APFS_TYPE_MAX = 15,

	APFS_TYPE_INVALID = 15
};

enum j_obj_kinds {
	APFS_KIND_ANY = 0,
	APFS_KIND_NEW = 1,
	APFS_KIND_UPDATE = 2,
	APFS_KIND_DEAD = 3,
	APFS_KIND_UPDATE_REFCNT = 4,

	APFS_KIND_INVALID = 255
};

enum j_inode_flags {
	INODE_IS_APFS_PRIVATE = 0x00000001,
	INODE_MAINTAIN_DIR_STATS = 0x00000002,
	INODE_DIR_STATS_ORIGIN = 0x00000004,
	INODE_PROT_CLASS_EXPLICIT = 0x00000008,
	INODE_WAS_CLONED = 0x00000010,
	INODE_FLAGS_UNUSED = 0x00000020,
	INODE_HAS_SECURITY_EA = 0x00000040,
	INODE_BEING_TRUNCATED = 0x00000080,
	INODE_HAS_FINDER_INFO = 0x00000100,
	INODE_IS_SPARSE = 0x00000200,
	INODE_WAS_EVER_CLONED = 0x00000400,
	INODE_ACTIVE_FILE_TRIMMED = 0x00000800,
	INODE_PINNED_TO_MAIN = 0x00001000,
	INODE_PINNED_TO_TIER2 = 0x00002000,
	INODE_HAS_RSRC_FORK = 0x00004000,
	INODE_NO_RSRC_FORK = 0x00008000,
	INODE_ALLOCATION_SPILLEDOVER = 0x00010000,
	INODE_FAST_PROMOTE = 0x00020000,
	INODE_HAS_UNCOMPRESSED_SIZE = 0x00040000,
	INODE_IS_PURGEABLE = 0x00080000,
	INODE_WANTS_TO_BE_PURGEABLE = 0x00100000,
	INODE_IS_SYNC_ROOT = 0x00200000,
	INODE_SNAPSHOT_COW_EXEMPTION = 0x00400000,

	INODE_INHERITED_INTERNAL_FLAGS = INODE_MAINTAIN_DIR_STATS | INODE_SNAPSHOT_COW_EXEMPTION,
	INODE_CLONED_INTERNAL_FLAGS = INODE_HAS_RSRC_FORK | INODE_NO_RSRC_FORK | INODE_HAS_FINDER_INFO | INODE_SNAPSHOT_COW_EXEMPTION
};

enum j_inode_bsd_flags {
	APFS_UF_NODUMP = 0x1,
	APFS_UF_IMMUTABLE = 0x2,
	APFS_UF_APPEND = 0x4,
	APFS_UF_OPAQUE = 0x8,
	APFS_UF_NOUNLINK = 0x10, // Reserved on macOS
	APFS_UF_COMPRESSED = 0x20,
	APFS_UF_TRACKED = 0x40,
	APFS_UF_DATAVAULT = 0x80,
	// 0x100 - 0x4000 reserved
	APFS_UF_HIDDEN = 0x8000,
	APFS_SF_ARCHIVED = 0x10000,
	APFS_SF_IMMUTABLE = 0x20000,
	APFS_SF_APPEND = 0x40000,
	APFS_SF_RESTRICTED = 0x80000,
	APFS_SF_NOUNLINK = 0x100000,
	APFS_SF_SNAPSHOT = 0x200000, // Reserved on macOS
	APFS_SF_FIRMLINK = 0x800000,
	APFS_SF_DATALESS = 0x40000000
};

constexpr uint64_t APFS_VALID_INTERNAL_INODE_FLAGS =
INODE_IS_APFS_PRIVATE |
INODE_MAINTAIN_DIR_STATS |
INODE_DIR_STATS_ORIGIN |
INODE_PROT_CLASS_EXPLICIT |
INODE_WAS_CLONED |
INODE_HAS_SECURITY_EA |
INODE_BEING_TRUNCATED |
INODE_HAS_FINDER_INFO |
INODE_IS_SPARSE |
INODE_WAS_EVER_CLONED |
INODE_ACTIVE_FILE_TRIMMED |
INODE_PINNED_TO_MAIN |
INODE_PINNED_TO_TIER2 |
INODE_HAS_RSRC_FORK |
INODE_NO_RSRC_FORK |
INODE_ALLOCATION_SPILLEDOVER |
INODE_FAST_PROMOTE |
INODE_HAS_UNCOMPRESSED_SIZE |
INODE_IS_PURGEABLE |
INODE_WANTS_TO_BE_PURGEABLE |
INODE_IS_SYNC_ROOT |
INODE_SNAPSHOT_COW_EXEMPTION;

enum j_xattr_flags {
	XATTR_DATA_STREAM = 0x0001,
	XATTR_DATA_EMBEDDED = 0x0002,
	XATTR_FILE_SYSTEM_OWNED = 0x0004,
	XATTR_RESERVED_8 = 0x0008
};

enum dir_rec_flags {
	DREC_TYPE_MASK = 0x000F,
	RESERVED_10 = 0x0010
};

constexpr uint64_t INVALID_INO_NUM = 0;
constexpr uint64_t ROOT_DIR_PARENT = 1;
constexpr uint64_t ROOT_DIR_INO_NUM = 2;
constexpr uint64_t PRIV_DIR_INO_NUM = 3;
constexpr uint64_t SNAP_DIR_INO_NUM = 6;
constexpr uint64_t PURGEABLE_DIR_INO_NUM = 7;
constexpr uint64_t MIN_USER_INO_NUM = 16;

constexpr uint64_t UNIFIED_ID_SPACE_MARK = 0x0800000000000000;


constexpr uint16_t XATTR_MAX_EMBEDDED_SIZE = 3804;
constexpr const char* SYMLINK_EA_NAME = "com.apple.fs.symlink";
constexpr const char* FIRMLINK_EA_NAME = "com.apple.fs.firmlink";
constexpr const char* APFS_COW_EXEMPT_COUNT_NAME = "com.apple.fs.cow-exempt-file-count";

constexpr uint64_t OWNING_OBJ_ID_INVALID = ~0ULL;
constexpr uint64_t OWNING_OBJ_ID_UNKNOWN = ~1ULL;

constexpr uint16_t JOBJ_MAX_KEY_SIZE = 832;
constexpr uint16_t JOBJ_MAX_VALUE_SIZE = 3808;

constexpr uint32_t MIN_DOC_ID = 3;


constexpr uint32_t FEXT_CRYPTO_ID_IS_TWEAK = 0x01;

constexpr uint16_t MODE_S_IFMT = 0170000;
constexpr uint16_t MODE_S_IFIFO = 0010000;
constexpr uint16_t MODE_S_IFCHR = 0020000;
constexpr uint16_t MODE_S_IFDIR = 0040000;
constexpr uint16_t MODE_S_IFBLK = 0060000;
constexpr uint16_t MODE_S_IFREG = 0100000;
constexpr uint16_t MODE_S_IFLNK = 0120000;
constexpr uint16_t MODE_S_IFSOCK = 0140000;
constexpr uint16_t MODE_S_IFWHT = 0160000;

constexpr uint16_t DT_UNKNOWN = 0;
constexpr uint16_t DT_FIFO = 1;
constexpr uint16_t DT_CHR = 2;
constexpr uint16_t DT_DIR = 4;
constexpr uint16_t DT_BLK = 6;
constexpr uint16_t DT_REG = 8;
constexpr uint16_t DT_LNK = 10;
constexpr uint16_t DT_SOCK = 12;
constexpr uint16_t DT_WHT = 14;


struct j_phys_ext_key_t {
	j_key_t hdr;
};

struct j_phys_ext_val_t {
	le_uint64_t len_and_kind;
	le_uint64_t owning_obj_id;
	le_uint32_t refcnt;
};

constexpr uint64_t PEXT_LEN_MASK = 0x0FFFFFFFFFFFFFFFULL;
constexpr uint64_t PEXT_KIND_MASK = 0xF000000000000000ULL;
constexpr int PEXT_KIND_SHIFT = 60;


struct j_file_extent_key_t {
	j_key_t hdr;
	le_uint64_t logical_addr;
};

struct j_file_extent_val_t {
	le_uint64_t len_and_flags;
	le_uint64_t phys_block_num;
	le_uint64_t crypto_id;
};

constexpr uint64_t J_FILE_EXTENT_LEN_MASK = 0x00FFFFFFFFFFFFFFULL;
constexpr uint64_t J_FILE_EXTENT_FLAG_MASK = 0xFF00000000000000ULL;
constexpr int J_FILE_EXTENT_FLAG_SHIFT = 56;


struct j_dstream_id_key_t {
	j_key_t hdr;
};

struct j_dstream_id_val_t {
	le_uint32_t refcnt;
};


struct j_dstream_t {
	le_uint64_t size;
	le_uint64_t alloced_size;
	le_uint64_t default_crypto_id;
	le_uint64_t total_bytes_written;
	le_uint64_t total_bytes_read;
};

struct j_xattr_dstream_t {
	le_uint64_t xattr_obj_id;
	j_dstream_t dstream;
};


struct xf_blob_t {
	le_uint16_t xf_num_exts;
	le_uint16_t xf_used_data;
	uint8_t xf_data[];
};

struct x_field_t {
	uint8_t x_type;
	uint8_t x_flags;
	le_uint16_t x_size;
};

enum {
	DREC_EXT_TYPE_SIBLING_ID = 1,

	INO_EXT_TYPE_SNAP_XID = 1,
	INO_EXT_TYPE_DELTRA_TREE_OID = 2,
	INO_EXT_TYPE_DOCUMENT_ID = 3,
	INO_EXT_TYPE_NAME = 4,
	INO_EXT_TYPE_PREV_FSIZE = 5,
	INO_EXT_TYPE_RESERVED_6 = 6,
	INO_EXT_TYPE_FINDER_INFO = 7,
	INO_EXT_TYPE_DSTREAM = 8,
	INO_EXT_TYPE_RESERVED_9 = 9,
	INO_EXT_TYPE_DIR_STATS_KEY = 10,
	INO_EXT_TYPE_FS_UUID = 11,
	INO_EXT_TYPE_RESERVED_12 = 12,
	INO_EXT_TYPE_SPARSE_BYTES = 13,
	INO_EXT_TYPE_RDEV = 14,
	INO_EXT_TYPE_PURGEABLE_FLAGS = 15,
	INO_EXT_TYPE_ORIG_SYNC_ROOT_ID = 16
};

constexpr uint8_t XF_DATA_DEPENDENT = 0x01;
constexpr uint8_t XF_DO_NOT_COPY = 0x02;
constexpr uint8_t XF_RESERVED_4 = 0x04;
constexpr uint8_t XF_CHILDREN_INHERIT = 0x08;
constexpr uint8_t XF_USER_FIELD = 0x10;
constexpr uint8_t XF_SYSTEM_FIELD = 0x20;
constexpr uint8_t XF_RESERVED_40 = 0x40;
constexpr uint8_t XF_RESERVED_80 = 0x80;


struct j_sibling_key_t {
	j_key_t hdr;
	le_uint64_t sibling_id;
};

struct j_sibling_val_t {
	le_uint64_t parent_id;
	le_uint16_t name_len;
	uint8_t name[0];
};


struct j_sibling_map_key_t {
	j_key_t hdr;
};

struct j_sibling_map_val_t {
	le_uint64_t file_id;
};


struct j_snap_metadata_key_t {
	j_key_t hdr;
};

struct j_snap_metadata_val_t {
	le_oid_t extentref_tree_oid;
	le_oid_t sblock_oid;
	le_uint64_t create_time;
	le_uint64_t change_time;
	le_uint64_t inum;
	le_uint32_t extentref_tree_type;
	le_uint32_t flags;
	le_uint16_t name_len;
	uint8_t name[0];
};


struct j_snap_name_key_t {
	j_key_t hdr;
	le_uint16_t name_len;
	uint8_t name[0];
};

struct j_snap_name_val_t {
	le_xid_t snap_xid;
};


enum snap_meta_flags {
	SNAP_META_PENDING_DATALESS = 0x00000001,
	SNAP_META_MERGE_IN_PROGRESS = 0x00000002
};


struct snap_meta_ext_t {
	le_uint32_t sme_version;

	le_uint32_t sme_flags;
	le_xid_t sme_snap_xid;
	apfs_uuid_t sme_uuid;

	le_uint64_t sme_token;
};

struct snap_meta_ext_obj_phys_t {
	obj_phys_t smeop_o;
	snap_meta_ext_t smeop_sme;
};


struct nloc_t {
	le_uint16_t off;
	le_uint16_t len;
};

struct btree_node_phys_t {
	obj_phys_t btn_o;
	le_uint16_t btn_flags;
	le_uint16_t btn_level;
	le_uint32_t btn_nkeys;
	nloc_t btn_table_space;
	nloc_t btn_free_space;
	nloc_t btn_key_free_list;
	nloc_t btn_val_free_list;
	// le_uint64_t btn_data[];
	uint8_t btn_data[];
};

struct btree_info_fixed_t {
	le_uint32_t bt_flags;
	le_uint32_t bt_node_size;
	le_uint32_t bt_key_size;
	le_uint32_t bt_val_size;
};

struct btree_info_t {
	btree_info_fixed_t bt_fixed;
	le_uint32_t bt_longest_key;
	le_uint32_t bt_longest_val;
	le_uint64_t bt_key_count;
	le_uint64_t bt_node_count;
};

constexpr uint16_t BTREE_NODE_HASH_SIZE_MAX = 64;

struct btn_index_node_val_t {
	le_oid_t binv_child_oid;
	uint8_t  binv_child_hash[BTREE_NODE_HASH_SIZE_MAX];
};

constexpr uint16_t BTOFF_INVALID = 0xFFFF;

struct kvloc_t {
	nloc_t k;
	nloc_t v;
};

struct kvoff_t {
	le_uint16_t k;
	le_uint16_t v;
};


constexpr uint32_t BTREE_UINT64_KEYS = 0x00000001;
constexpr uint32_t BTREE_SEQUENTIAL_INSERT = 0x00000002;
constexpr uint32_t BTREE_ALLOW_GHOSTS = 0x00000004;
constexpr uint32_t BTREE_EPHEMERAL = 0x00000008;
constexpr uint32_t BTREE_PHYSICAL = 0x00000010;
constexpr uint32_t BTREE_NONPERSISTENT = 0x00000020;
constexpr uint32_t BTREE_KV_NONALIGNED = 0x00000040;
constexpr uint32_t BTREE_HASHED = 0x00000080;
constexpr uint32_t BTREE_NOHEADER = 0x00000100;

constexpr int BTREE_TOC_ENTRY_INCREMENT = 8;
constexpr int BTREE_TOC_ENTRY_MAX_UNUSED = 2 * BTREE_TOC_ENTRY_INCREMENT;

constexpr uint16_t BTNODE_ROOT = 0x0001;
constexpr uint16_t BTNODE_LEAF = 0x0002;
constexpr uint16_t BTNODE_FIXED_KV_SIZE = 0x0004;
constexpr uint16_t BTNODE_HASHED = 0x0008;
constexpr uint16_t BTNODE_NOHEADER = 0x0010;

constexpr uint16_t BTNODE_CHECK_KOFF_INVAL = 0x8000;


constexpr uint16_t BTREE_NODE_SIZE_DEFAULT = 4096;
constexpr uint32_t BTREE_NODE_MIN_ENTRY_COUNT = 4;

enum {
	INTEGRITY_META_VERSION_INVALID = 0,
	INTEGRITY_META_VERSION_1 = 1,
	INTEGRITY_META_VERSION_2 = 2,
	INTEGRITY_META_VERSION_HIGHEST = INTEGRITY_META_VERSION_2
};

constexpr uint32_t APFS_SEAL_BROKEN = 1;

enum apfs_hash_type_t {
	APFS_HASH_INVALID = 0,
	APFS_HASH_SHA256 = 1,
	APFS_HASH_SHA512_256 = 2,
	APFS_HASH_SHA384 = 3,
	APFS_HASH_SHA512 = 4,

	APFS_HASH_MIN = APFS_HASH_SHA256,
	APFS_HASH_MAX = APFS_HASH_SHA512,

	APFS_HASH_DEFAULT = APFS_HASH_SHA256
};

constexpr int APFS_HASH_CCSHA256_SIZE = 32;
constexpr int APFS_HASH_CCSHA512_256_SIZE = 32;
constexpr int APFS_HASH_CCSHA384_SIZE = 48;
constexpr int APFS_HASH_CCSHA512_SIZE = 64;

constexpr int APFS_HASH_MAX_SIZE = 64;

struct integrity_meta_phys_t {
	obj_phys_t im_o;
	le_uint32_t im_version;
	le_uint32_t im_flags;
	le_uint32_t im_hash_type;
	le_uint32_t im_root_hash_offset;
	le_xid_t im_broken_xid;
	le_uint64_t im_reserved[9];
};

struct fext_tree_key_t {
	le_uint64_t private_id;
	le_uint64_t logical_addr;
};

struct fext_tree_val_t {
	le_uint64_t len_and_flags;
	le_uint64_t phys_block_num;
};

struct j_file_info_key_t {
	j_key_t hdr;
	le_uint64_t info_and_lba;
};

constexpr uint64_t J_FILE_INFO_LBA_MASK = 0x00FFFFFFFFFFFFFF;
constexpr uint64_t J_FILE_INFO_TYPE_MASK = 0xFF00000000000000;
constexpr int J_FILE_INFO_TYPE_SHIFT = 56;

struct j_file_data_hash_val_t {
	le_uint16_t hashed_len;
	uint8_t hash_size;
	uint8_t hash[0];
};

struct j_file_info_val_t {
	union {
		j_file_data_hash_val_t dhash;
	};
};

enum j_obj_file_info_type {
	APFS_FILE_INFO_DATA_HASH = 1
};


struct chunk_info_t {
	le_uint64_t ci_xid;
	le_uint64_t ci_addr;
	le_uint32_t ci_block_count;
	le_uint32_t ci_free_count;
	le_paddr_t ci_bitmap_addr;
};

struct chunk_info_block_t {
	obj_phys_t cib_o;
	le_uint32_t cib_index;
	le_uint32_t cib_chunk_info_count;
	chunk_info_t cib_chunk_info[];
};

struct cib_addr_block_t {
	obj_phys_t cab_o;
	le_uint32_t cab_index;
	le_uint32_t cab_cib_count;
	le_paddr_t cab_cib_addr[];
};

struct spaceman_free_queue_key_t {
	le_xid_t sfqk_xid;
	le_paddr_t sfqk_paddr;
};

typedef le_uint64_t spaceman_free_queue_val_t;

struct spaceman_free_queue_entry_t {
	spaceman_free_queue_key_t   sfqe_key;
	spaceman_free_queue_val_t   sfqe_count;
};

struct spaceman_free_queue_t {
	le_uint64_t sfq_count;
	le_oid_t sfq_tree_oid;
	le_xid_t sfq_oldest_xid;
	le_uint16_t sfq_tree_node_limit;
	le_uint16_t sfq_pad16;
	le_uint32_t sfq_pad32;
	le_uint64_t sfq_reserved;
};

struct spaceman_device_t {
	le_uint64_t sm_block_count;
	le_uint64_t sm_chunk_count;
	le_uint32_t sm_cib_count;
	le_uint32_t sm_cab_count;
	le_uint64_t sm_free_count;
	le_uint32_t sm_addr_offset;
	le_uint32_t sm_reserved;
	le_uint64_t sm_reserved2;
};

constexpr int SM_ALLOCZONE_INVALID_END_BOUNDARY = 0;
constexpr int SM_ALLOCZONE_NUM_PREVIOUS_BOUNDARIES = 7;
constexpr int SM_DATAZONE_ALLOCZONE_COUNT = 8;

enum sfq {
	SFQ_IP = 0,
	SFQ_MAIN = 1,
	SFQ_TIER2 = 2,
	SFQ_COUNT = 3
};

enum smdev {
	SD_MAIN = 0,
	SD_TIER2 = 1,
	SD_COUNT = 2
};

struct spaceman_allocation_zone_boundaries_t {
	le_uint64_t saz_zone_start;
	le_uint64_t saz_zone_end;
};

struct spaceman_allocation_zone_info_phys_t {
	spaceman_allocation_zone_boundaries_t saz_current_boundaries;
	spaceman_allocation_zone_boundaries_t saz_previous_boundaries[SM_ALLOCZONE_NUM_PREVIOUS_BOUNDARIES];
	le_uint16_t saz_zone_id;
	le_uint16_t saz_previous_boundary_index;
	le_uint32_t saz_reserved;
};

struct spaceman_datazone_info_phys_t {
	spaceman_allocation_zone_info_phys_t sdz_allocation_zones[SD_COUNT][SM_DATAZONE_ALLOCZONE_COUNT];
};

struct spaceman_phys_t {
	obj_phys_t sm_o;
	le_uint32_t sm_block_size;
	le_uint32_t sm_blocks_per_chunk;
	le_uint32_t sm_chunks_per_cib;
	le_uint32_t sm_cibs_per_cab;
	spaceman_device_t sm_dev[SD_COUNT];
	le_uint32_t sm_flags;
	le_uint32_t sm_ip_bm_tx_multiplier;
	le_uint64_t sm_ip_block_count;
	le_uint32_t sm_ip_bm_size_in_blocks;
	le_uint32_t sm_ip_bm_block_count;
	le_paddr_t sm_ip_bm_base;
	le_paddr_t sm_ip_base;
	le_uint64_t sm_fs_reserve_block_count;
	le_uint64_t sm_fs_reserve_alloc_count;
	spaceman_free_queue_t sm_fq[SFQ_COUNT];
	le_uint16_t sm_ip_bm_free_head;
	le_uint16_t sm_ip_bm_free_tail;
	le_uint32_t sm_ip_bm_xid_offset;
	le_uint32_t sm_ip_bitmap_offset;
	le_uint32_t sm_ip_bm_free_next_offset;
	le_uint32_t sm_version;
	le_uint32_t sm_struct_size;
	spaceman_datazone_info_phys_t sm_datazone;
};

constexpr uint32_t SM_FLAG_VERSIONED = 0x00000001;

constexpr uint32_t CI_COUNT_MASK = 0x000FFFFF;
constexpr uint32_t CI_COUNT_RESERVED_MASK = 0xFFF00000;


constexpr uint32_t SPACEMAN_IP_BM_TX_MULTIPLIER = 16;
constexpr uint16_t SPACEMAN_IP_BM_INDEX_INVALID = 0xFFFF;
constexpr uint16_t SPACEMAN_IP_BM_BLOCK_COUNT_MAX = 0xFFFE;


struct nx_reaper_phys_t {
	obj_phys_t nr_o;
	le_uint64_t nr_next_reap_id;
	le_uint64_t nr_completed_id;
	le_oid_t nr_head;
	le_oid_t nr_tail;
	le_uint32_t nr_flags;
	le_uint32_t nr_rlcount;
	le_uint32_t nr_type;
	le_uint32_t nr_size;
	le_oid_t nr_fs_oid;
	le_oid_t nr_oid;
	le_xid_t nr_xid;
	le_uint32_t nr_nrle_flags;
	le_uint32_t nr_state_buffer_size;
	uint8_t nr_state_buffer[];
};

struct nx_reap_list_entry_t {
	le_uint32_t nrle_next;
	le_uint32_t nrle_flags;
	le_uint32_t nrle_type;
	le_uint32_t nrle_size;
	le_oid_t nrle_fs_oid;
	le_oid_t nrle_oid;
	le_xid_t nrle_xid;
};

struct nx_reap_list_phys_t {
	obj_phys_t nrl_o;
	le_oid_t nrl_next;
	le_uint32_t nrl_flags;
	le_uint32_t nrl_max;
	le_uint32_t nrl_count;
	le_uint32_t nrl_first;
	le_uint32_t nrl_last;
	le_uint32_t nrl_free;
	nx_reap_list_entry_t nrl_entries[];
};

enum {
	APFS_REAP_PHASE_START = 0,
	APFS_REAP_PHASE_SNAPSHOTS = 1,
	APFS_REAP_PHASE_ACTIVE_FS = 2,
	APFS_REAP_PHASE_DESTROY_OMAP = 3,
	APFS_REAP_PHASE_DONE = 4
};

constexpr uint32_t NR_BHM_FLAG = 0x00000001;
constexpr uint32_t NR_CONTINUE = 0x00000002;

constexpr uint32_t NRLE_VALID = 0x00000001;
constexpr uint32_t NRLE_REAP_ID_RECORD = 0x00000002;
constexpr uint32_t NRLE_CALL = 0x00000004;
constexpr uint32_t NRLE_COMPLETION = 0x00000008;
constexpr uint32_t NRLE_CLEANUP = 0x00000010;

constexpr uint32_t NRL_INDEX_INVALID = 0xFFFFFFFF;

struct omap_reap_state_t {
	le_uint32_t omr_phase;
	omap_key_t omr_ok;
};


struct omap_cleanup_state_t {
	le_uint32_t omc_cleaning;
	le_uint32_t omc_omsflags;
	le_xid_t omc_sxidprev;
	le_xid_t omc_sxidstart;
	le_xid_t omc_sxidend;
	le_xid_t omc_sxidnext;
	omap_key_t omc_curkey;
};

struct apfs_reap_state_t {
	le_uint64_t last_pbn;
	le_xid_t cur_snap_xid;
	le_uint32_t phase;
};


struct j_crypto_key_t {
	j_key_t hdr;
};

struct j_crypto_val_t {
	le_uint32_t refcnt;
	wrapped_crypto_state_t state;
};


constexpr uint32_t CP_EFFECTIVE_CLASSMASK = 0x0000001F;

constexpr int CP_IV_KEYSIZE = 16;
constexpr int CP_MAX_KEYSIZE = 32;
constexpr int CP_MAX_CACHEBUFLEN = 64;

constexpr int CP_INITIAL_WRAPPEDKEYSIZE = 40;
constexpr int CP_V2_WRAPPEDKEYSIZE = 40;
constexpr int CP_V4_RESERVEDBYTES = 16;

constexpr int CP_MAX_WRAPPEDKEYSIZE = 128;

constexpr int CP_VERS_4 = 4;
constexpr int CP_VERS_5 = 5;
constexpr int CP_MINOR_VERS = 0;
constexpr int CP_CURRENT_VERS = CP_VERS_5;

constexpr int PROTECTION_CLASS_DIR_NONE = 0;
constexpr int PROTECTION_CLASS_A = 1;
constexpr int PROTECTION_CLASS_B = 2;
constexpr int PROTECTION_CLASS_C = 3;
constexpr int PROTECTION_CLASS_D = 4;
constexpr int PROTECTION_CLASS_F = 6;

constexpr int CRYPTO_SW_ID = 4;
constexpr int CRYPTO_VOLKEY_ID = 5;


struct keybag_entry_t {
	apfs_uuid_t ke_uuid;
	le_uint16_t ke_tag;
	le_uint16_t ke_keylen;
	uint8_t _padding_[4];
	uint8_t ke_keydata[];
};

struct kb_locker_t {
	le_uint16_t kl_version;
	le_uint16_t kl_nkeys;
	le_uint32_t kl_nbytes;
	uint8_t _padding_[8];
	uint8_t kl_entries[]; /* keybag_entry_t */
};

struct mk_obj_t {
	uint8_t o_cksum[MAX_CKSUM_SIZE];
	le_oid_t o_oid;
	le_xid_t o_xid;
	le_uint32_t o_type;
	le_uint32_t o_subtype;
};

struct media_keybag_t {
	mk_obj_t mk_obj;
	kb_locker_t mk_locker;
};

constexpr uint16_t APFS_KEYBAG_VERSION = 2;
constexpr uint32_t APFS_KEYBAG_OBJ = 0x6B657973; // 'keys';
constexpr uint32_t APFS_VOL_KEYBAG_OBJ = 0x72656373; // 'recs';
constexpr uint16_t APFS_VOL_KEYBAG_ENTRY_MAX_SIZE = 512;
constexpr uint16_t APFS_ENTRY_ALIGN = 16;

enum {
	KB_TAG_UNKNOWN = 0,
	KB_TAG_WRAPPING_KEY,
	KB_TAG_VOLUME_KEY,
	KB_TAG_VOLUME_UNLOCK_RECORDS,
	KB_TAG_VOLUME_PASSPHRASE_HINT,
	KB_TAG_USER_PAYLOAD
};

struct er_state_phys_header_t {
	obj_phys_t ersb_o;
	le_uint32_t ersb_magic;
	le_uint32_t ersb_version;
};

struct er_state_phys_t {
	er_state_phys_header_t ersb_header;
	le_uint64_t ersb_flags;
	le_uint64_t ersb_snap_xid;
	le_uint64_t ersb_current_fext_obj_id;
	le_uint64_t ersb_file_offset;
	le_uint64_t ersb_progress;
	le_uint64_t ersb_total_blk_to_encrypt;
	le_oid_t ersb_blockmap_oid;
	le_uint64_t ersb_tidemark_obj_id;
	le_uint64_t ersb_recovery_extents_count;
	le_oid_t ersb_recovery_list_oid;
	le_uint64_t ersb_recovery_length;
};

struct er_state_phys_v1_t {
	er_state_phys_header_t ersb_header;
	le_uint64_t ersb_flags;
	le_uint64_t ersb_snap_xid;
	le_uint64_t ersb_current_fext_obj_id;
	le_uint64_t ersb_file_offset;
	le_uint64_t ersb_fext_pbn;
	le_uint64_t ersb_paddr;
	le_uint64_t ersb_progress;
	le_uint64_t ersb_total_blk_to_encrypt;
	le_uint64_t ersb_blockmap_oid;
	le_uint32_t ersb_checksum_count;
	le_uint32_t ersb_reserved;
	le_uint64_t ersb_fext_cid;
	uint8_t ersb_checksum[0];
};

enum er_phase_t {
	ER_PHASE_OMAP_ROLL = 1,
	ER_PHASE_DATA_ROLL = 2,
	ER_PHASE_SNAP_ROLL = 3
};

struct er_recovery_block_phys_t {
	obj_phys_t erb_o;
	le_uint64_t erb_offset;
	le_oid_t erb_next_oid;
	uint8_t erb_data[0];
};

struct gbitmap_block_phys_t {
	obj_phys_t bmb_o;
	le_uint64_t bmb_field[0];
};

struct gbitmap_phys_t {
	obj_phys_t bm_o;
	le_oid_t bm_tree_oid;
	le_uint64_t bm_bit_count;
	le_uint64_t bm_flags;
};

enum {
	ER_512B_BLOCKSIZE = 0,
	ER_2KiB_BLOCKSIZE = 1,
	ER_4KiB_BLOCKSIZE = 2,
	ER_8KiB_BLOCKSIZE = 3,
	ER_16KiB_BLOCKSIZE = 4,
	ER_32KiB_BLOCKSIZE = 5,
	ER_64KiB_BLOCKSIZE = 6
};

constexpr uint32_t ERSB_FLAG_ENCRYPTING = 0x00000001;
constexpr uint32_t ERSB_FLAG_DECRYPTING = 0x00000002;
constexpr uint32_t ERSB_FLAG_KEYROLLING = 0x00000004;
constexpr uint32_t ERSB_FLAG_PAUSED = 0x00000008;
constexpr uint32_t ERSB_FLAG_FAILED = 0x00000010;
constexpr uint32_t ERSB_FLAG_CID_IS_TWEAK = 0x00000020;
constexpr uint32_t ERSB_FLAG_FREE_1 = 0x00000040;
constexpr uint32_t ERSB_FLAG_FREE_2 = 0x00000080;

constexpr uint32_t ERSB_FLAG_CM_BLOCK_SIZE_MASK = 0x00000F00;
constexpr int ERSB_FLAG_CM_BLOCK_SIZE_SHIFT = 8;

constexpr uint32_t ERSB_FLAG_ER_PHASE_MASK = 0x00003000;
constexpr int ERSB_FLAG_ER_PHASE_SHIFT = 12;
constexpr uint32_t ERSB_FLAG_FROM_ONEKEY = 0x00004000;

constexpr int ER_CHECKSUM_LENGTH = 8;
constexpr uint32_t ER_MAGIC = 0x464C4142; // 'FLAB';
constexpr uint32_t ER_VERSION = 1;

constexpr int ER_MAX_CHECKSUM_COUNT_SHIFT = 16;
constexpr uint32_t ER_CUR_CHECKSUM_COUNT_MASK = 0x0000FFFF;


struct fusion_wbc_phys_t {
	obj_phys_t fwp_objHdr;
	le_uint64_t fwp_version;
	le_oid_t fwp_listHeadOid;
	le_oid_t fwp_listTailOid;
	le_uint64_t fwp_stableHeadOffset;
	le_uint64_t fwp_stableTailOffset;
	le_uint32_t fwp_listBlocksCount;
	le_uint32_t fwp_reserved;
	le_uint64_t fwp_usedByRC;
	prange_t fwp_rcStash;
};

struct fusion_wbc_list_entry_t {
	le_paddr_t fwle_wbcLba;
	le_paddr_t fwle_targetLba;
	le_uint64_t fwle_length;
};

struct fusion_wbc_list_phys_t {
	obj_phys_t fwlp_objHdr;
	le_uint64_t fwlp_version;
	le_uint64_t fwlp_tailOffset;
	le_uint32_t fwlp_indexBegin;
	le_uint32_t fwlp_indexEnd;
	le_uint32_t fwlp_indexMax;
	le_uint32_t fwlp_reserved;
	fusion_wbc_list_entry_t fwlp_listEntries[];
};

constexpr uint64_t FUSION_TIER2_DEVICE_BYTE_ADDR = 0x4000000000000000ULL;
// FUSION_TIER2_DEVICE_BLOCK_ADDR
// FUSION_BLKNO()

struct fusion_mt_key_t {
	le_paddr_t paddr;
};

struct fusion_mt_val_t {
	le_paddr_t fmv_lba;
	le_uint32_t fmv_length;
	le_uint32_t fmv_flags;
};

constexpr uint32_t FUSION_MT_DIRTY = 1 << 0;
constexpr uint32_t FUSION_MT_TENANT = 1 << 1;



#define APFS_TYPE_ID(t, o) ((static_cast<uint64_t>(t) << OBJ_TYPE_SHIFT) | (o & OBJ_ID_MASK))


#ifdef _MSC_VER
#pragma warning(default: 4200)
#endif

#pragma pack(pop)

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

#include "Global.h"
#include "Endian.h"

constexpr uint64_t KeyType_SnapMetadata = 0x1000000000000000ULL;
constexpr uint64_t KeyType_PhysExtent = 0x2000000000000000ULL;
constexpr uint64_t KeyType_Inode = 0x3000000000000000ULL;
constexpr uint64_t KeyType_Xattr = 0x4000000000000000ULL;
constexpr uint64_t KeyType_SiblingLink = 0x5000000000000000ULL;
constexpr uint64_t KeyType_DstreamID = 0x6000000000000000ULL;
constexpr uint64_t KeyType_CryptoState = 0x7000000000000000ULL;
constexpr uint64_t KeyType_FileExtent = 0x8000000000000000ULL;
constexpr uint64_t KeyType_DirRecord = 0x9000000000000000ULL;
constexpr uint64_t KeyType_DirSize = 0xA000000000000000ULL;
constexpr uint64_t KeyType_SnapName = 0xB000000000000000ULL;
constexpr uint64_t KeyType_SiblingMap = 0xC000000000000000ULL;

enum BlockType
{
	BlockType_NXSB = 1,
	BlockType_BTRoot = 2,
	BlockType_BTNode = 3,
	BlockType_MTree = 4, // Unknown
	BlockType_SpacemanHeader = 5,
	BlockType_CAB = 6, // Chunk something Block
	BlockType_ChunkInfoBlock = 7,
	BlockType_SpacemanIP = 8, // Unknown
	BlockType_BTreeRootPtr = 11,
	BlockType_CheckPointMap = 12,
	BlockType_APSB = 13,
	BlockType_NR = 17, // Unknown
	BlockType_NRL = 18, // Unknown
	BlockType_WBC = 22, // Unknown
	BlockType_WBCL = 23, // Unknown
	// 0x14: Pointer to EFI driver, pointed to by NXSB/4F8, signature 'JSDR'
	// 0x18: Unknown, signature 'BALF', Length 0x880. Contains pointer to 0x19 ...
	// 0x19: U64 Pointer to B-Tree type 0x1A, U32 Unknown
	// 0x1B: No idea, looks like some kind of bitmap ...
};

enum XFType
{
	DREC_EXT_TYPE_SIBLING_ID = 1,
	INO_EXT_TYPE_DOCUMENT_ID = 3,
	INO_EXT_TYPE_NAME = 4,
	INO_EXT_TYPE_DSTREAM = 8,
	INO_EXT_TYPE_DIR_STATS_KEY = 10,
	INO_EXT_TYPE_FS_UUID = 11,
	INO_EXT_TYPE_SPARSE_BYTES = 13
};

#pragma pack(push)
#pragma pack(1)

struct APFS_ObjHeader
{
	le<uint64_t> o_cksum;
	le<uint64_t> o_oid;
	le<uint64_t> o_xid;
	le<uint32_t> o_type;
	le<uint32_t> o_subtype;
};

#define APFS_OBJ_TYPE(x) (x & 0xFFFFFFF)

/*
Known Flags:
0x1000 Encrypted
0x4000 Belongs to volume
0x8000 Belongs to container
*/

static_assert(sizeof(APFS_ObjHeader) == 0x20, "BlockHeader size wrong");

struct APFS_TableHeader // at 0x20
{
	le<uint16_t> page;
	le<uint16_t> level;
	le<uint32_t> entries_cnt;
};

static_assert(sizeof(APFS_TableHeader) == 0x8, "TableHeader size wrong");

struct APFS_BTHeader // at 0x20
{
	le<uint16_t> flags; // Bit 0x4 : Fixed
	le<uint16_t> level; // 0 = Leaf
	le<uint32_t> key_count;
	le<uint16_t> table_space_offset;
	le<uint16_t> table_space_length;
	le<uint16_t> free_space_offset;
	le<uint16_t> free_space_length;
	le<uint16_t> key_free_list_space_offset;
	le<uint16_t> key_free_list_space_length;
	le<uint16_t> val_free_list_space_offset;
	le<uint16_t> val_free_list_space_length;
};

static_assert(sizeof(APFS_BTHeader) == 0x18, "BTHeader size wrong");

struct APFS_BTFooter
{
	le<uint32_t> unk_FD8;
	le<uint32_t> nodesize; // 0x1000 - Node Size?
	le<uint32_t> key_size; // Key Size - or 0
	le<uint32_t> val_size; // Value Size - or 0
	le<uint32_t> key_max_size; // (Max) Key Size - or 0
	le<uint32_t> val_max_size; // (Max) Value Size - or 0
	le<uint64_t> entries_cnt; // Total entries in B*-Tree
	le<uint64_t> nodes_cnt; // Total nodes in B*-Tree
};

static_assert(sizeof(APFS_BTFooter) == 0x28, "BTFooter size wrong");

struct APFS_BTEntry
{
	le<uint16_t> key_offs; // offs = base + key_offs
	le<uint16_t> key_len;
	le<uint16_t> value_offs; // offs = 0x1000 - value_offs
	le<uint16_t> value_len;
};

struct APFS_BTEntryFixed
{
	le<uint16_t> key_offs;
	le<uint16_t> value_offs;
};

// Entries in Bitmap Table

struct APFS_Chunk
{
	le<uint64_t> xid;
	le<uint64_t> offset;
	le<uint32_t> bits_total;
	le<uint32_t> bits_avail;
	le<uint64_t> block;
};

/* -------------------------------------------------------------------------- */

// Type = 1
struct APFS_SnapMetadata_Val
{
	le<uint64_t> extentref_tree_oid; // Blockid of 4/F (BlkID->ObjID)
	le<uint64_t> sblock_oid; // Blockid of 4/D (Superblock)
	le<uint64_t> change_time;
	le<uint64_t> create_time;
	le<uint64_t> inum;
	le<uint32_t> extentref_tree_type;
	le<uint32_t> flags;
	le<uint16_t> name_len;
	// name here
};

// Type = 2
struct APFS_ExtentRef_Val
{
	le<uint64_t> len;
	le<uint64_t> owning_obj_id;
	le<uint32_t> refcnt;
};

// Type = 3
struct APFS_Inode_Val
{
	le<uint64_t> parent_id;
	le<uint64_t> private_id;
	le<uint64_t> birthtime;
	le<uint64_t> mtime;
	le<uint64_t> ctime;
	le<uint64_t> atime;
	le<uint64_t> internal_flags; // ? usually 0x8000
	le<uint32_t> nchildren; // Only for hard links
	le<uint32_t> default_protection_class;
	le<uint32_t> gen_count;
	le<uint32_t> bsd_flags;  // 0x20: compressed
	le<uint32_t> uid; // uid (somehow modified)
	le<uint32_t> gid; // gid (somehow modified)
	le<uint16_t> mode;
	le<uint16_t> pad1;
	le<uint64_t> uncompressed_size; // Usually 0, sometimes contains the decompressed size of compressed files
	// XF may follow here ...
};

struct APFS_DStream // INO_EXT_TYPE_DSTREAM
{
	le<uint64_t> size;
	le<uint64_t> alloced_size;
	le<uint64_t> default_crypto_id;
	le<uint64_t> unk_18;
	le<uint64_t> unk_20;
};

// Type = 4
struct APFS_Xattr_Key
{
	le<uint64_t> inode_key;
	le<uint16_t> name_len;
	char name[0x400];
};

struct APFS_Xattr_Val
{
	le<uint16_t> type;
	le<uint16_t> size;
};

struct APFS_Xattr_External
{
	le<uint64_t> obj_id;
	// Rest is a dstream
	APFS_DStream stream;
};

// Type = 5
struct APFS_Sibling_Key
{
	le<uint64_t> obj_id;
	le<uint64_t> sibling_id;
};

struct APFS_Sibling_Val
{
	le<uint64_t> parent_id;
	le<uint16_t> name_len;
	char name[0x400];
};

// Type = 6
struct APFS_DStream_Val
{
	le<uint32_t> refcnt;
};

// Type = 7
struct APFS_CryptoState
{
	le<uint16_t> major_version;
	le<uint16_t> minor_version;
	le<uint32_t> cpflags;
	le<uint32_t> persistent_class;
	le<uint32_t> key_os_version;
	le<uint16_t> key_revision;
	le<uint16_t> key_len;
};

struct APFS_Crypto_Val
{
	le<uint32_t> refcnt;
	APFS_CryptoState state;
};

// Type = 8
struct APFS_FileExtent_Key
{
	le<uint64_t> obj_id;
	le<uint64_t> logical_addr;
};

struct APFS_FileExtent_Val
{
	le<uint64_t> flags_length; // TODO: flags:8 length:56
	le<uint64_t> phys_block_num;
	le<uint64_t> crypto_id;
};

// Type = 9
struct APFS_DRec_Key_Old
{
	le<uint64_t> parent_id;
	le<uint16_t> name_len;
	char name[0x400];
};

struct APFS_DRec_Key
{
	le<uint64_t> parent_id;
	le<uint32_t> hash; // Lowest byte is the name len
	char name[0x400];
};

struct APFS_DRec_Val
{
	le<uint64_t> file_id;
	le<uint64_t> date_added;
	le<uint16_t> flags;
	// XF may follow here ...
};

// Type = 10
struct APFS_DirStats_Val
{
	le<uint64_t> num_children;
	le<uint64_t> total_size;
	le<uint64_t> chained_key;
	le<uint64_t> gen_count;
};

// Type = 11
struct APFS_SnapName_Key
{
	le<uint64_t> obj_id;
	le<uint16_t> name_len;
	char name[0x400];
};

struct APFS_SnapName_Val
{
	le<uint64_t> snap_xid;
};

// Type = 12
struct APFS_SiblingMap_Val
{
	le<uint64_t> file_id;
};

/* -------------------------------------------------------------------------- */

struct APFS_XF_Header
{
	le<uint16_t> xf_num_exts;
	le<uint16_t> xf_used_data;
};

struct APFS_XF_Entry
{
	le<uint8_t> xf_type;
	le<uint8_t> xf_flags;
	le<uint16_t> xf_length;
};

/* -------------------------------------------------------------------------- */

struct APFS_CPM_Map
{
	le<uint32_t> type; // type?
	le<uint32_t> subtype;
	le<uint64_t> unk_08; // size?
	le<uint64_t> unk_10;
	le<uint64_t> nid;
	le<uint64_t> block;
};

struct APFS_OMap_Key
{
	le<uint64_t> ok_oid;
	le<uint64_t> ok_xid;
};

struct APFS_OMap_Val
{
	le<uint32_t> ov_flags;
	le<uint32_t> ov_size;
	le<uint64_t> ov_paddr;
};

struct APFS_Value_10_B
{
	le<uint64_t> id;
};

struct APFS_Key_8_9
{
	le<uint64_t> xid;
	le<uint64_t> bid; // Block-ID
	// Value = Number of blocks
	// If no value, the number of blocks is 1
};

struct APFS_NX_Superblock // Ab 0x20
{
	APFS_ObjHeader hdr;
	le<uint32_t> nx_magic; // 'NXSB' / 0x4253584E
	le<uint32_t> nx_block_size;
	le<uint64_t> nx_block_count;
	le<uint64_t> nx_features;
	le<uint64_t> nx_read_only_compatible_features;
	le<uint64_t> nx_incompatible_features;
	apfs_uuid_t nx_uuid;
	le<uint64_t> nx_next_oid; // Next node id (?)
	le<uint64_t> nx_next_xid; // Next transaction id (?)
	le<uint32_t> nx_xp_desc_blocks; // Number of blocks for NXSB + 4_C ?
	le<uint32_t> nx_xp_data_blocks; // Number of blocks for the rest
	le<uint64_t> nx_xp_desc_base; // Block-ID (0x4000000C) - No
	le<uint64_t> nx_xp_data_base; // Block-ID (0x80000005) => Node-ID 0x400
	le<uint32_t> nx_xp_desc_next; // Next 4_C + NXSB? (+sb_area_start)
	le<uint32_t> nx_xp_data_next; // Next 8_5/2/11? (+blockid_spaceman_area_start)
	le<uint32_t> nx_xp_desc_index; // Start 4_C+NXSB block (+sb_area_start)
	le<uint32_t> nx_xp_desc_len; // Length 4_C+NXSB block
	le<uint32_t> nx_xp_data_index; // Start 8_5/2/11 blocks (+blockid_spaceman_area_start)
	le<uint32_t> nx_xp_data_len; // No of 8_5/2/11 blocks
	le<uint64_t> nx_spaceman_oid;     // Node-ID (0x400) => (0x80000005)
	le<uint64_t> nx_omap_oid; // Block-ID => (0x4000000B) => B*-Tree for mapping node-id -> volume APSB superblocks
	le<uint64_t> nx_reaper_oid;    // Node-ID (0x401) => (0x80000011)
	le<uint32_t> unk_B0;
	le<uint32_t> nx_max_file_systems;
	le<uint64_t> nx_fs_oid[100]; // List of the node-id's of the volume superblocks
	le<uint64_t> unk_3D8[0x20];
	le<uint64_t> nx_blocked_out_base;
	le<uint64_t> nx_blocked_out_blocks;
	le<uint64_t> unk_4E8;
	le<uint64_t> unk_4F0;
	le<uint64_t> unk_4F8[3];
	le<uint64_t> nx_keybag_base;
	le<uint64_t> nx_keybag_blocks;
	le<uint64_t> nx_ephemeral_info[5];
	le<uint64_t> nx_fusion_mt_oid;
	le<uint64_t> nx_fusion_wbc_oid;
	le<uint64_t> nx_fusion_wbc_base;
	le<uint64_t> nx_fusion_wbc_blocks;
	// There's some more stuff here, but I have no idea about it's meaning ...
};

static_assert(sizeof(APFS_NX_Superblock) == 0x568, "NXSB Superblock size wrong");

struct APFS_Superblock_APSB_AccessInfo
{
	char id[0x20];
	le<uint64_t> timestamp;
	le<uint64_t> last_xid;
};

constexpr uint64_t APFS_APSB_CaseSensitive = 8;
constexpr uint64_t APFS_APSB_CaseInsensitive = 1;

struct APFS_Superblock_APSB
{
	APFS_ObjHeader hdr;
	le<uint32_t> apfs_magic; // APSB, 0x42535041
	le<uint32_t> apfs_fs_index;
	le<uint64_t> apfs_features; // Features 3
	le<uint64_t> apfs_readonly_compatible_features; // Features 2
    le<uint64_t> apfs_incompatible_features; // Features: & 0x09: 0x00 = old format (iOS), 0x01 = case-insensitive, 0x08 = case-sensitive
	le<uint64_t> apfs_unmount_time;
	le<uint64_t> apfs_reserve_block_count;
	le<uint64_t> apfs_quota_block_count;
	le<uint64_t> apfs_fs_alloc_count; // Node-ID
	le<uint64_t> unk_60;
	le<uint32_t> unk_68;
	le<uint32_t> unk_6C;
	le<uint32_t> unk_70;
	le<uint32_t> apfs_root_tree_type;
	le<uint32_t> apfs_extentref_tree_type; // 40000002
	le<uint32_t> apfs_snap_meta_tree_type; // 40000002
	le<uint64_t> apfs_omap_oid; // Block ID -> 4000000B -> Node Map Tree Root (40000002/B) - Node Map only for Directory!
	le<uint64_t> apfs_root_tree_oid; // Node ID -> Root Directory
	le<uint64_t> apfs_extentref_tree_oid; // Block ID -> 40000002/F Block Map Tree Root
	le<uint64_t> apfs_snap_meta_tree_oid; // Block ID -> Root of 40000002/10 Tree
	le<uint64_t> apfs_revert_to_xid;
	le<uint64_t> unk_A8;
	le<uint64_t> apfs_next_obj_id;
	le<uint64_t> apfs_num_files;
	le<uint64_t> apfs_num_directories;
	le<uint64_t> apfs_num_symlinks;
	le<uint64_t> apfs_num_other_fsobjects;
	le<uint64_t> apfs_num_snapshots;
	le<uint64_t> apfs_total_blocks_alloced;
	le<uint64_t> apfs_total_blocks_freed;
	apfs_uuid_t apfs_vol_uuid;
	le<uint64_t> apfs_last_mod_time;
    le<uint64_t> apfs_fs_flags; // TODO: No version, but flags: Bit 0x1: 0 = Encrypted, 1 = Unencrypted. Bit 0x2: Effaceable
	APFS_Superblock_APSB_AccessInfo apfs_formatted_by;
	APFS_Superblock_APSB_AccessInfo apfs_modified_by[8];
	char apfs_volname[0x100];
	le<uint32_t> apfs_next_doc_id;
	le<uint32_t> unk_3C4;
	le<uint64_t> unk_3C8;
	// 3D0: crypto state?
};

static_assert(sizeof(APFS_Superblock_APSB) == 0x3D0, "APSB Superblock size wrong");

struct APFS_Block_4_7_Bitmaps
{
	APFS_ObjHeader hdr;
	APFS_TableHeader tbl;
	APFS_Chunk bmp[0x7E];
};

static_assert(sizeof(APFS_Block_4_7_Bitmaps) == 0xFE8, "Block-40000007 size wrong");

struct APFS_Entry_4_B
{
	le<uint32_t> type_1;
	le<uint32_t> type_2;
	le<uint64_t> blk;
};

struct APFS_Block_4_B_BTreeRootPtr
{
	APFS_ObjHeader hdr;
	APFS_TableHeader tbl;
	APFS_Entry_4_B entry[0xFD];
};

static_assert(sizeof(APFS_Block_4_B_BTreeRootPtr) == 0xFF8, "Block-4000000B size wrong");

struct APFS_Block_4_C
{
	APFS_ObjHeader hdr;
	APFS_TableHeader tbl;
	APFS_CPM_Map entry[0x65];
};

static_assert(sizeof(APFS_Block_4_C) == 0xFF0, "Block-4000000C size wrong");

struct APFS_NX_Reaper // 0x40000011
{
	APFS_ObjHeader hdr;
	le<uint64_t> unk_20;
	le<uint64_t> unk_28;
	le<uint64_t> unk_30;
	le<uint64_t> unk_38;
	le<uint32_t> unk_40;
	le<uint32_t> unk_44;
	le<uint32_t> unk_48;
	le<uint32_t> unk_4C;
	le<uint64_t> unk_50;
	le<uint64_t> unk_58;
	le<uint64_t> unk_60;
	le<uint32_t> unk_68;
	le<uint32_t> unk_6C;
};

struct APFS_NX_ReapList_Entry
{
	le<uint32_t> fwlink;
	le<uint32_t> unk_04;
	le<uint32_t> type;
	le<uint32_t> blksize;
	le<uint64_t> oid;
	le<uint64_t> paddr;
	le<uint64_t> xid;
};

struct APFS_NX_ReapList // 0x40000012
{
	APFS_ObjHeader hdr;
	le<uint32_t> unk_20;
	le<uint32_t> unk_24;
	le<uint32_t> unk_28;
	le<uint32_t> max_record_count;
	le<uint32_t> record_count;
	le<uint32_t> first_index;
	le<uint32_t> last_index;
	le<uint32_t> free_index;
	APFS_NX_ReapList_Entry nrle[0x64];
};

struct APFS_Block_8_5_Spaceman
{
	APFS_ObjHeader hdr;
	le<uint32_t> block_size;
	le<uint32_t> blocks_per_chunk;
	le<uint32_t> chunks_per_cib;
	le<uint32_t> cibs_per_cab;
	le<uint64_t> block_count;
	le<uint64_t> chunk_count;
	le<uint32_t> cib_count;
	le<uint32_t> cab_count;
	le<uint64_t> free_count;
	le<uint64_t> cib_arr_offs;
	le<uint64_t> unk_58; // cab_arr_offs ?
	le<uint64_t> tier2_block_count;
	le<uint64_t> tier2_chunk_count;
	le<uint32_t> tier2_cib_count;
	le<uint32_t> tier2_cab_count;
	le<uint64_t> tier2_free_count;
	le<uint64_t> tier2_cib_arr_offs;
	le<uint64_t> unk_88; // tier2_cab_arr_offs ?
	le<uint32_t> unk_90;
	le<uint32_t> unk_94;
	le<uint64_t> ip_block_count; // Container-Bitmaps
	le<uint32_t> ip_bm_block_count;
	le<uint32_t> ip_bitmap_block_count; // Mgr-Bitmaps?
	le<uint64_t> ip_bm_base_address; // Mgr-Bitmaps
	le<uint64_t> ip_base_address; // Container-Bitmaps
	le<uint64_t> unk_B8;
	le<uint64_t> unk_C0;
	le<uint64_t> free_queue_count_1;
	le<uint64_t> free_queue_tree_1; // Obsolete B*-Tree fuer Mgr-Bitmap-Blocks
	le<uint64_t> unk_D8;
	le<uint64_t> unk_E0;
	le<uint64_t> unk_E8;
	le<uint64_t> free_queue_count_2;
	le<uint64_t> free_queue_tree_2; // Obsolete B*-Tree fuer Volume;
	le<uint64_t> unk_100;
	le<uint64_t> unk_108;
	le<uint64_t> unk_110;
	le<uint64_t> free_queue_count_3;
	le<uint64_t> free_queue_tree_3;
	le<uint64_t> unk_128;
	le<uint64_t> unk_130;
	le<uint64_t> unk_138;
	le<uint16_t> bitmap_next_array_free;
	le<uint16_t> unk_142;
	le<uint32_t> unk_144; // Bitmap-Array-Offset?
	le<uint32_t> unk_148;
	le<uint32_t> unk_14C;
	le<uint64_t> unk_150;
	le<uint64_t> unk_158;
	le<uint16_t> unk_160[0x10];
	le<uint64_t> blockid_vol_bitmap_hdr;
	uint8_t      unk_188[0x850];
	le<uint64_t> some_xid_9D8;
	le<uint64_t> unk_9E0;
	le<uint16_t> unk_9E8[0x10];
	le<uint64_t> bid_bmp_hdr_list[0xBF];
	// Ab A08 bid bitmap-header
};

static_assert(sizeof(APFS_Block_8_5_Spaceman) == 0x1000, "Spaceman Header wrong size");

#pragma pack(pop)

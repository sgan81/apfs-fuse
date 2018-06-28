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

constexpr uint64_t KeyType_Snapshot_1 = 0x1000000000000000ULL;
constexpr uint64_t KeyType_BlockMap = 0x2000000000000000ULL;
constexpr uint64_t KeyType_Object = 0x3000000000000000ULL;
constexpr uint64_t KeyType_Attribute = 0x4000000000000000ULL;
constexpr uint64_t KeyType_HardLink_5 = 0x5000000000000000ULL;
constexpr uint64_t KeyType_RefCount = 0x6000000000000000ULL;
constexpr uint64_t KeyType_Unknown_7 = 0x7000000000000000ULL;
constexpr uint64_t KeyType_Extent = 0x8000000000000000ULL;
constexpr uint64_t KeyType_Name = 0x9000000000000000ULL;
constexpr uint64_t KeyType_Unknown_A = 0xA000000000000000ULL;
constexpr uint64_t KeyType_Snapshot_B = 0xB000000000000000ULL;
constexpr uint64_t KeyType_Hardlink_C = 0xC000000000000000ULL;

enum BlockType
{
	BlockType_NXSB = 1,
	BlockType_BTRoot = 2,
	BlockType_BTNode = 3,
	BlockType_SpacemanHeader = 5,
	BlockType_BitmapHeader = 7,
	BlockType_BTreeRootPtr = 11,
	BlockType_IDMapping = 12,
	BlockType_APSB = 13
	// 0x14: Pointer to EFI driver, pointed to by NXSB/4F8, signature 'JSDR'
	// 0x18: Unknown, signature 'BALF', Length 0x880. Contains pointer to 0x19 ...
	// 0x19: U64 Pointer to B-Tree type 0x1A, U32 Unknown
	// 0x1B: No idea, looks like some kind of bitmap ...
};

#pragma pack(push)
#pragma pack(1)

struct APFS_BlockHeader
{
	le<uint64_t> checksum;
	le<uint64_t> nid;
	le<uint64_t> xid;
	le<uint16_t> type;
	le<uint16_t> flags; // See below
	le<uint32_t> subtype;
};

/*
Known Flags:
0x1000 Encrypted
0x4000 Belongs to volume
0x8000 Belongs to container
*/

static_assert(sizeof(APFS_BlockHeader) == 0x20, "BlockHeader size wrong");

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
	le<uint32_t> entries_cnt;
	le<uint16_t> keys_offs;
	le<uint16_t> keys_len;
	le<uint16_t> free_offs;
	le<uint16_t> free_len;
	le<uint16_t> unk_30;
	le<uint16_t> unk_32;
	le<uint16_t> unk_34;
	le<uint16_t> unk_36;
};

static_assert(sizeof(APFS_BTHeader) == 0x18, "BTHeader size wrong");

struct APFS_BTFooter
{
	le<uint32_t> unk_FD8;
	le<uint32_t> unk_FDC; // 0x1000 - Node Size?
	le<uint32_t> min_key_size; // Key Size - or 0
	le<uint32_t> min_val_size; // Value Size - or 0
	le<uint32_t> max_key_size; // (Max) Key Size - or 0
	le<uint32_t> max_val_size; // (Max) Value Size - or 0
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

struct APFS_BitmapPtr
{
	le<uint64_t> xid;
	le<uint64_t> offset;
	le<uint32_t> bits_total;
	le<uint32_t> bits_avail;
	le<uint64_t> block;
};

// Entries in BT 0/E

struct APFS_Inode
{
	le<uint64_t> parent_id;
	le<uint64_t> object_id;
	le<uint64_t> birthtime;
	le<uint64_t> mtime;
	le<uint64_t> ctime;
	le<uint64_t> atime;
	le<uint64_t> unk_30; // ? usually 0x8000
	le<uint64_t> refcnt;
	le<uint32_t> unk_40;
	le<uint32_t> flags;  // 0x20: compressed
	le<uint32_t> uid; // uid (somehow modified)
	le<uint32_t> gid; // gid (somehow modified)
	le<uint64_t> mode;
	le<uint32_t> unk_58;
	le<uint16_t> entries_cnt;
	le<uint16_t> entries_len;
};

struct APFS_InodeEntry
{
	le<uint16_t> type; // No idea ...
	le<uint16_t> len; // Pad to multiple of 8
};

struct APFS_Inode_Sizes // Object, after filename
{
	le<uint64_t> size;
	le<uint64_t> size_on_disk;
	le<uint64_t> unk_10;
	le<uint64_t> size_2;
	le<uint64_t> unk_20;
};

struct APFS_Key_Name_Old
{
	le<uint64_t> parent_id;
	le<uint16_t> name_len;
	char name[0x400];
};

struct APFS_Key_Name
{
	le<uint64_t> parent_id;
	le<uint32_t> hash; // Lowest byte is the name len
	char name[0x400];
};

struct APFS_Name
{
	le<uint64_t> id;
	le<uint64_t> timestamp;
	le<uint16_t> unk;
};

struct APFS_Name_SLink
{
	le<uint16_t> unk_12;
	le<uint16_t> unk_14;
	le<uint8_t> unk_16;
	le<uint8_t> unk_17;
	le<uint16_t> unk_18;
	le<uint64_t> obj_id;
};

struct APFS_Key_Extent
{
	le<uint64_t> inode;
	le<uint64_t> offset;
};

struct APFS_Extent
{
	le<uint64_t> size;
	le<uint64_t> block;
	le<uint64_t> xts_blkid;
};

struct APFS_Key_Attribute
{
	le<uint64_t> inode_key;
	le<uint16_t> name_len;
	char name[0x400];
};

struct APFS_Attribute
{
	le<uint16_t> type;
	le<uint16_t> size;
};

struct APFS_AttributeLink
{
	le<uint64_t> object_id;
	le<uint64_t> size;
	le<uint64_t> size_on_disk;
	le<uint64_t> unk[3];
};

struct APFS_IDMapping
{
	le<uint32_t> type; // type?
	le<uint32_t> subtype;
	le<uint64_t> unk_08; // size?
	le<uint64_t> unk_10;
	le<uint64_t> nid;
	le<uint64_t> block;
};

struct APFS_Key_B_NodeID_Map
{
	le<uint64_t> nid;
	le<uint64_t> xid;
};

struct APFS_Value_B_NodeID_Map
{
	le<uint32_t> flags;
	le<uint32_t> size;
	le<uint64_t> bid;
};

struct APFS_Value_F
{
	le<uint64_t> block_cnt;
	le<uint64_t> oid;
	le<uint32_t> unk_10;
};

struct APFS_Value_10_1_Snapshot
{
	le<uint64_t> bid_bomap; // Blockid of 4/F (BlkID->ObjID)
	le<uint64_t> bid_apsb; // Blockid of 4/D (Superblock)
	le<uint64_t> tstamp_10;
	le<uint64_t> tstamp_18;
	le<uint64_t> unk_20;
	le<uint32_t> type_28;
	le<uint32_t> unk_2C;
	le<uint16_t> namelen;
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

struct APFS_Superblock_NXSB // Ab 0x20
{
	APFS_BlockHeader hdr;
	le<uint32_t> signature; // 'NXSB' / 0x4253584E
	le<uint32_t> block_size;
	le<uint64_t> block_count;
	le<uint64_t> unk_30;
	le<uint64_t> unk_38;
	le<uint64_t> unk_40;
	apfs_uuid_t container_guid;
	le<uint64_t> next_nid; // Next node id (?)
	le<uint64_t> next_xid; // Next transaction id (?)
	le<uint32_t> sb_area_cnt; // Number of blocks for NXSB + 4_C ?
	le<uint32_t> spaceman_area_cnt; // Number of blocks for the rest
	le<uint64_t> bid_sb_area_start; // Block-ID (0x4000000C) - No
	le<uint64_t> bid_spaceman_area_start; // Block-ID (0x80000005) => Node-ID 0x400
	le<uint32_t> next_sb; // Next 4_C + NXSB? (+sb_area_start)
	le<uint32_t> next_spaceman; // Next 8_5/2/11? (+blockid_spaceman_area_start)
	le<uint32_t> current_sb_start; // Start 4_C+NXSB block (+sb_area_start)
	le<uint32_t> current_sb_len; // Length 4_C+NXSB block
	le<uint32_t> current_spaceman_start; // Start 8_5/2/11 blocks (+blockid_spaceman_area_start)
	le<uint32_t> current_spaceman_len; // No of 8_5/2/11 blocks
	le<uint64_t> nid_spaceman;     // Node-ID (0x400) => (0x80000005)
	le<uint64_t> bid_nodemap; // Block-ID => (0x4000000B) => B*-Tree for mapping node-id -> volume APSB superblocks
	le<uint64_t> nid_8x11;    // Node-ID (0x401) => (0x80000011)
	le<uint32_t> unk_B0;
	le<uint32_t> unk_B4;
	le<uint64_t> nid_apsb[100]; // List of the node-id's of the volume superblocks (not sure about the length of this list though ...)
	le<uint64_t> unk_3D8[0x23];
	le<uint64_t> unk_4F0[4];
	le<uint64_t> keybag_blk_start;
	le<uint64_t> keybag_blk_count;
	le<uint64_t> unk_520;
	// There's some more stuff here, but I have no idea about it's meaning ...
};

static_assert(sizeof(APFS_Superblock_NXSB) == 0x528, "NXSB Superblock size wrong");

struct APFS_Superblock_APSB_AccessInfo
{
	char accessor[0x20];
	le<uint64_t> timestamp;
	le<uint64_t> xid;
};

struct APFS_Superblock_APSB
{
	APFS_BlockHeader hdr;
	le<uint32_t> signature; // APSB, 0x42535041
	le<uint32_t> unk_24;
	le<uint64_t> features_28; // Features 3
	le<uint64_t> features_30; // Features 2
    le<uint64_t> features_38; // Features: & 0x09: 0x00 = old format (iOS), 0x01 = case-insensitive, 0x08 = case-sensitive
	le<uint64_t> unk_40;
	le<uint64_t> blocks_reserved;
	le<uint64_t> blocks_quota;
	le<uint64_t> unk_58; // Node-ID
	le<uint64_t> unk_60;
	le<uint32_t> unk_68;
	le<uint32_t> unk_6C;
	le<uint32_t> unk_70;
	le<uint32_t> unk_74;
	le<uint32_t> unk_78; // 40000002
	le<uint32_t> unk_7C; // 40000002
	le<uint64_t> blockid_nodemap; // Block ID -> 4000000B -> Node Map Tree Root (40000002/B) - Node Map only for Directory!
	le<uint64_t> nodeid_rootdir; // Node ID -> Root Directory
	le<uint64_t> blockid_blockmap; // Block ID -> 40000002/F Block Map Tree Root
	le<uint64_t> blockid_4xBx10_map; // Block ID -> Root of 40000002/10 Tree
	le<uint64_t> unk_A0;
	le<uint64_t> unk_A8;
	le<uint64_t> unk_B0;
	le<uint64_t> unk_B8;
	le<uint64_t> unk_C0;
	le<uint64_t> unk_C8;
	le<uint64_t> unk_D0;
	le<uint64_t> unk_D8;
	le<uint64_t> unk_E0;
	le<uint64_t> unk_E8;
	apfs_uuid_t guid;
	le<uint64_t> timestamp_100;
    le<uint64_t> flags_108; // TODO: No version, but flags: Bit 0x1: 0 = Encrypted, 1 = Unencrypted. Bit 0x2: Effaceable
	APFS_Superblock_APSB_AccessInfo access_info[9];
	char vol_name[0x100];
	le<uint64_t> unk_3C0;
	le<uint64_t> unk_3C8;
};

static_assert(sizeof(APFS_Superblock_APSB) == 0x3D0, "APSB Superblock size wrong");

struct APFS_Block_4_7_Bitmaps
{
	APFS_BlockHeader hdr;
	APFS_TableHeader tbl;
	APFS_BitmapPtr bmp[0x7E];
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
	APFS_BlockHeader hdr;
	APFS_TableHeader tbl;
	APFS_Entry_4_B entry[0xFD];
};

static_assert(sizeof(APFS_Block_4_B_BTreeRootPtr) == 0xFF8, "Block-4000000B size wrong");

struct APFS_Block_4_C
{
	APFS_BlockHeader hdr;
	APFS_TableHeader tbl;
	APFS_IDMapping entry[0x65];
};

static_assert(sizeof(APFS_Block_4_C) == 0xFF0, "Block-4000000C size wrong");

struct APFS_Block_8_5_Spaceman
{
	APFS_BlockHeader hdr;
	le<uint32_t> unk_20;
	le<uint32_t> unk_24;
	le<uint32_t> unk_28;
	le<uint32_t> unk_2C;
	le<uint64_t> blocks_total;
	le<uint64_t> unk_38;
	le<uint64_t> unk_40;
	le<uint64_t> blocks_free;
	le<uint64_t> unk_50;
	le<uint64_t> unk_58;
	le<uint64_t> unk_60;
	le<uint64_t> unk_68;
	le<uint64_t> unk_70;
	le<uint64_t> unk_78;
	le<uint64_t> unk_80;
	le<uint64_t> unk_88;
	le<uint32_t> unk_90;
	le<uint32_t> unk_94;
	le<uint64_t> blockcnt_bitmaps_98; // Container-Bitmaps
	le<uint32_t> unk_A0;
	le<uint32_t> blockcnt_bitmaps_A4; // Mgr-Bitmaps?
	le<uint64_t> blockid_begin_bitmaps_A8; // Mgr-Bitmaps
	le<uint64_t> blockid_bitmaps_B0; // Container-Bitmaps
	le<uint64_t> unk_B8;
	le<uint64_t> unk_C0;
	le<uint64_t> unk_C8;
	le<uint64_t> nodeid_obsolete_1; // Obsolete B*-Tree fuer Mgr-Bitmap-Blocks
	le<uint64_t> unk_D8;
	le<uint64_t> unk_E0;
	le<uint64_t> unk_E8;
	le<uint64_t> unk_F0;
	le<uint64_t> nodeid_obsolete_2; // Obsolete B*-Tree fuer Volume;
	le<uint64_t> unk_100;
	le<uint64_t> unk_108;
	le<uint64_t> unk_110;
	le<uint64_t> unk_118;
	le<uint64_t> unk_120;
	le<uint64_t> unk_128;
	le<uint64_t> unk_130;
	le<uint64_t> unk_138;
	le<uint16_t> unk_140;
	le<uint16_t> unk_142;
	le<uint32_t> unk_144;
	le<uint32_t> unk_148;
	le<uint32_t> unk_14C;
	le<uint64_t> unk_150;
	le<uint64_t> unk_158;
	le<uint16_t> unk_160[0x10];
	le<uint64_t> blockid_vol_bitmap_hdr;
	// Ab A08 bid bitmap-header
};

static_assert(sizeof(APFS_Block_8_5_Spaceman) == 0x188, "Spaceman Header wrong size");

#pragma pack(pop)

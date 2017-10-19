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

#pragma pack(push)
#pragma pack(1)

struct APFS_GUID
{
	uint32_t data_1;
	uint16_t data_2;
	uint16_t data_3;
	uint8_t  data_4[8];
};

struct APFS_BlockHeader
{
	uint64_t checksum;
	uint64_t node_id;
	uint64_t version;
	uint32_t type;
	uint32_t subtype;
};

static_assert(sizeof(APFS_BlockHeader) == 0x20, "BlockHeader size wrong");

struct APFS_TableHeader // at 0x20
{
	uint16_t page;
	uint16_t level;
	uint32_t entries_cnt;
};

static_assert(sizeof(APFS_TableHeader) == 0x8, "TableHeader size wrong");

struct APFS_BTHeader // at 0x20
{
	uint16_t flags; // Bit 0x4 : Fixed
	uint16_t level; // 0 = Leaf
	uint32_t entries_cnt;
	uint16_t keys_offs;
	uint16_t keys_len;
	uint16_t free_offs;
	uint16_t free_len;
	uint16_t unk_30;
	uint16_t unk_32;
	uint16_t unk_34;
	uint16_t unk_36;
};

static_assert(sizeof(APFS_BTHeader) == 0x18, "BTHeader size wrong");

struct APFS_BTFooter
{
	uint32_t unk_FD8;
	uint32_t unk_FDC; // 0x1000 - Node Size?
	uint32_t min_key_size; // Key Size - or 0
	uint32_t min_val_size; // Value Size - or 0
	uint32_t max_key_size; // (Max) Key Size - or 0
	uint32_t max_val_size; // (Max) Value Size - or 0
	uint64_t entries_cnt; // Total entries in B*-Tree
	uint64_t nodes_cnt; // Total nodes in B*-Tree
};

static_assert(sizeof(APFS_BTFooter) == 0x28, "BTFooter size wrong");

struct APFS_BTEntry
{
	uint16_t key_offs; // offs = Base + key_offs
	uint16_t key_len;
	uint16_t value_offs; // offs = 0x1000 - value_offs
	uint16_t value_len;
};

struct APFS_BTEntryFixed
{
	uint16_t key_offs;
	uint16_t value_offs;
};

// Entries in Bitmap Table

struct APFS_BitmapPtr
{
	uint64_t version;
	uint64_t offset;
	uint32_t bits_total;
	uint32_t bits_avail;
	uint64_t block;
};

// Entries in BT 0/E

struct APFS_Inode
{
	uint64_t parent_id;
	uint64_t object_id;
	uint64_t birthtime;
	uint64_t mtime;
	uint64_t ctime;
	uint64_t atime;
	uint64_t unk_30; // ? meistens 0x8000
	uint64_t refcnt;
	uint32_t unk_40;
	uint32_t flags;  // 0x20: compressed
	uint32_t uid; // uid (somehow modified)
	uint32_t gid; // gid (somehow modified)
	uint64_t mode;
	uint32_t unk_58;
	uint16_t entries_cnt;
	uint16_t entries_len;
};

struct APFS_InodeEntry
{
	uint16_t type; // Keine Ahnung ...
	uint16_t len; // Padden auf vielfaches von 8 ...
};

struct APFS_Inode_Sizes // Object, after Filename
{
	uint64_t size;
	uint64_t size_on_disk;
	uint64_t unk_10;
	uint64_t size_2;
	uint64_t unk_20;
};

struct APFS_Key_Name_Old
{
	uint64_t parent_id;
	uint16_t name_len;
	char name[0x400];
};

struct APFS_Key_Name
{
	uint64_t parent_id;
	uint32_t hash;
	char name[0x400];
};

struct APFS_Name
{
	uint64_t id;
	uint64_t timestamp;
	uint16_t unk;
};

struct APFS_Name_SLink
{
	uint16_t unk_12;
	uint16_t unk_14;
	uint8_t unk_16;
	uint8_t unk_17;
	uint16_t unk_18;
	uint64_t obj_id;
};

struct APFS_Key_Extent
{
	uint64_t inode;
	uint64_t offset;
};

struct APFS_Extent
{
	uint64_t size;
	uint64_t block;
	uint64_t unk;
};

struct APFS_Key_Attribute
{
	uint64_t inode_key;
	uint16_t name_len;
	char name[0x400];
};

struct APFS_Attribute
{
	uint16_t type;
	uint16_t size;
};

struct APFS_AttributeLink
{
	uint64_t object_id;
	uint64_t size;
	uint64_t size_on_disk;
	uint64_t unk[3];
};

struct APFS_IDMapping
{
	uint32_t type; // type?
	uint32_t subtype;
	uint64_t unk_08; // size?
	uint64_t unk_10;
	uint64_t nodeid;
	uint64_t block;
};

struct APFS_Key_B_NodeID_Map
{
	uint64_t nodeid;
	uint64_t version;
};

struct APFS_Value_B_NodeID_Map
{
	uint32_t flags;
	uint32_t size;
	uint64_t blockid;
};

struct APFS_Value_F
{
	uint64_t block_cnt;
	uint64_t obj_id;
	uint32_t unk_10;
};

struct APFS_Value_10_1
{
	uint64_t unk_00;
	uint64_t unk_08;
	uint64_t tstamp_10;
	uint64_t tstamp_18;
	uint64_t unk_20;
	uint32_t type_28;
	uint32_t unk_2C;
	uint16_t namelen;
};

struct APFS_Value_10_B
{
	uint64_t id;
};

struct APFS_Key_8_9
{
	uint64_t version;
	uint64_t blk_id; // Block-ID
	// => Anzahl Bloecke
	// Wenn kein Value, dann Anzahl = 1
};

struct APFS_Superblock_NXSB // Ab 0x20
{
	APFS_BlockHeader hdr;
	uint32_t signature; // 'NXSB' / 0x4253584E
	uint32_t block_size;
	uint64_t block_count;
	uint64_t unk_30;
	uint64_t unk_38;
	uint64_t unk_40;
	APFS_GUID container_guid;
	uint64_t next_nodeid; // Next node id (?)
	uint64_t next_version; // Next version number (?)
	uint32_t sb_area_cnt; // Anzahl Bloecke fuer NXSB + 4_C ?
	uint32_t spaceman_area_cnt; // Anzahl Bloecke fuer Rest
	uint64_t blockid_sb_area_start; // Block-ID (0x4000000C) - Nein
	uint64_t blockid_spaceman_area_start; // Block-ID (0x80000005) => Node-ID 0x400
	uint32_t next_sb; // Naechster 4_C + NXSB? (+sb_area_start)
	uint32_t next_spaceman; // Naechster 8_5/2/11? (+blockid_spaceman_area_start)
	uint32_t current_sb_start; // Start 4_C+NXSB Block (+sb_area_start)
	uint32_t current_sb_len; // Laenge 4_C+NXSB Block
	uint32_t current_spaceman_start; // Start 8_5/2/11 Bloecke (+blockid_spaceman_area_start)
	uint32_t current_spaceman_len; // Anzahl 8_5/2/11 Bloecke
	uint64_t nodeid_8x5;     // Node-ID (0x400) => (0x80000005)
	uint64_t blockid_volhdr; // Block-ID => (0x4000000B) => B*-Tree fuer Mapping Node-ID -> Volume APSB Superblocks
	uint64_t nodeid_8x11;    // Node-ID (0x401) => (0x80000011)
	uint32_t unk_B0;
	uint32_t unk_B4;
	uint64_t nodeid_apsb[100]; // Liste der Node-ID's der Volume Superblocks (Anzahl geraten, koennte aber hinkommen ...)
	// Hier kaeme noch ein wenig mehr ... aber keine Ahnung was es bedeutet ...
};

static_assert(sizeof(APFS_Superblock_NXSB) == 0x3D8, "NXSB Superblock size wrong");

struct APFS_Superblock_APSB_AccessInfo
{
	char accessor[0x20];
	uint64_t timestamp;
	uint64_t version;
};

struct APFS_Superblock_APSB
{
	APFS_BlockHeader hdr;
	uint32_t signature; // APSB, 0x42535041
	uint32_t unk_24;
	uint64_t unk_28;
	uint64_t unk_30;
	uint64_t unk_38;
	uint64_t unk_40;
	uint64_t blocks_reserved;
	uint64_t blocks_quota;
	uint64_t unk_58; // Node-ID
	uint64_t unk_60;
	uint32_t unk_68;
	uint32_t unk_6C;
	uint32_t unk_70;
	uint32_t unk_74;
	uint32_t unk_78; // 40000002
	uint32_t unk_7C; // 40000002
	uint64_t blockid_nodemap; // Block ID -> 4000000B -> Node Map Tree Root (40000002/B) - Node Map nur fuer Directory!
	uint64_t nodeid_rootdir; // Node ID -> Root Directory
	uint64_t blockid_blockmap; // Block ID -> 40000002/F Block Map Tree Root
	uint64_t blockid_4xBx10_map; // Block ID -> Root of 40000002/10 Tree
	uint64_t unk_A0;
	uint64_t unk_A8;
	uint64_t unk_B0;
	uint64_t unk_B8;
	uint64_t unk_C0;
	uint64_t unk_C8;
	uint64_t unk_D0;
	uint64_t unk_D8;
	uint64_t unk_E0;
	uint64_t unk_E8;
	APFS_GUID guid;
	uint64_t timestamp_100;
	uint64_t version_108;
	APFS_Superblock_APSB_AccessInfo access_info[9];
	char vol_name[0x100];
	uint64_t unk_3C0;
	uint64_t unk_3C8;
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
	uint32_t type_1;
	uint32_t type_2;
	uint64_t blk;
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
	uint32_t unk_20;
	uint32_t unk_24;
	uint32_t unk_28;
	uint32_t unk_2C;
	uint64_t blocks_total;
	uint64_t unk_38;
	uint64_t unk_40;
	uint64_t blocks_free;
	uint64_t unk_50;
	uint64_t unk_58;
	uint64_t unk_60;
	uint64_t unk_68;
	uint64_t unk_70;
	uint64_t unk_78;
	uint64_t unk_80;
	uint64_t unk_88;
	uint32_t unk_90;
	uint32_t unk_94;
	uint64_t blockcnt_bitmaps_98; // Container-Bitmaps
	uint32_t unk_A0;
	uint32_t blockcnt_bitmaps_A4; // Mgr-Bitmaps?
	uint64_t blockid_begin_bitmaps_A8; // Mgr-Bitmaps
	uint64_t blockid_bitmaps_B0; // Container-Bitmaps
	uint64_t unk_B8;
	uint64_t unk_C0;
	uint64_t unk_C8;
	uint64_t nodeid_obsolete_1; // Obsolete B*-Tree fuer Mgr-Bitmap-Blocks
	uint64_t unk_D8;
	uint64_t unk_E0;
	uint64_t unk_E8;
	uint64_t unk_F0;
	uint64_t nodeid_obsolete_2; // Obsolete B*-Tree fuer Volume;
	uint64_t unk_100;
	uint64_t unk_108;
	uint64_t unk_110;
	uint64_t unk_118;
	uint64_t unk_120;
	uint64_t unk_128;
	uint64_t unk_130;
	uint64_t unk_138;
	uint16_t unk_140;
	uint16_t unk_142;
	uint32_t unk_144;
	uint32_t unk_148;
	uint32_t unk_14C;
	uint64_t unk_150;
	uint64_t unk_158;
	uint16_t unk_160[0x10];
	uint64_t blockid_vol_bitmap_hdr;
};

static_assert(sizeof(APFS_Block_8_5_Spaceman) == 0x188, "Spaceman Header wrong size");

#pragma pack(pop)

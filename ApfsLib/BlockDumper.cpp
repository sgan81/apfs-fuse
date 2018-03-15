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
#include "Util.h"

#include <ctime>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cstring>

using namespace std;

BlockDumper::BlockDumper(std::ostream &os, size_t blocksize) :
	m_os(os),
	m_blocksize(blocksize)
{
	m_block = nullptr;
	m_text_flags = 0x08; // Standard: Case-sensitive
}

BlockDumper::~BlockDumper()
{
}

void BlockDumper::DumpNode(const byte_t *block, uint64_t blk_nr)
{
	using namespace std;

	m_block = block;

	m_os << hex << uppercase << setfill('0');

	const APFS_BlockHeader * const node = reinterpret_cast<const APFS_BlockHeader *>(m_block);
	// const APFS_BTHeader * const bt = reinterpret_cast<const APFS_BTHeader *>(m_block + 0x28);

	if (IsEmptyBlock(m_block, m_blocksize))
	{
		m_os << setw(16) << blk_nr << " [Empty]" << endl;
		m_block = nullptr;
		return;
	}

	if (!VerifyBlock(m_block, m_blocksize))
	{
		m_os << setw(16) << blk_nr << " !!! CHECKSUM ERROR !!!" << endl;
		m_block = nullptr;
		return;
	}

#if 0
	if (node->type != 0x0000000D)
		return;
#endif

	DumpNodeHeader(node, blk_nr);

	switch (node->type & 0xCFFFFFFF)
	{
	case 0x00000002: // Directory Root
	case 0x00000003: // Directory
		DumpBTNode_0();
		break;
	case 0x0000000D: // APSB Block
		DumpBlk_0_D();
		break;

	case 0x40000002: // Mapping Root
	case 0x40000003: // Mapping
		DumpBTNode_4();
		break;
	case 0x40000007: // Bitmap Block List
		DumpBlk_4_7();
		break;
	case 0x4000000B: // Pointer to Header (?)
		DumpBlk_4_B();
		break;
	case 0x4000000C: // Another Mapping
		DumpBlk_4_C();
		break;

	case 0x80000001: // NXSB Block
		DumpBlk_8_1();
		break;
	case 0x80000002:
	case 0x80000003:
		DumpBTNode_8();
		break;
	case 0x80000005:
		DumpBlk_8_5();
		break;
	case 0x80000011:
		DumpBlk_8_11();
		break;

	default:
		// assert(false);
		std::cerr << "!!! UNKNOWN NODE TYPE " << setw(8) << node->type << " in block " << setw(16) << blk_nr << " !!!" << endl;
		DumpBlockHex();
		break;
	}

	m_os << endl;
	m_os << "========================================================================================================================" << endl;
	m_os << endl;

	m_block = nullptr;
}

void BlockDumper::DumpNodeHeader(const APFS_BlockHeader *blk, uint64_t blk_nr)
{
	m_os << "[Block]          | Checksum         | Node ID          | Version          | Type     | Subtype  | Description" << endl;
	m_os << "-----------------+------------------+------------------+------------------+----------+----------+-----------------------" << endl;

	m_os << setw(16) << blk_nr << " | ";
	m_os << setw(16) << blk->checksum << " | ";
	m_os << setw(16) << blk->node_id << " | ";
	m_os << setw(16) << blk->version << " | ";
	m_os << setw(8) << blk->type << " | ";
	m_os << setw(8) << blk->subtype << " | ";
	m_os << GetNodeType(blk->type, blk->subtype) << endl;
	m_os << endl;
}


void BlockDumper::DumpBTNode(DumpFunc func, uint16_t keys_size, uint16_t values_size)
{
	const APFS_BlockHeader * const hdr = reinterpret_cast<const APFS_BlockHeader *>(m_block);
	const APFS_BTHeader * const bt = reinterpret_cast<const APFS_BTHeader *>(m_block + 0x20);
	const bool is_index = bt->level > 0;

	const byte_t *key_ptr = nullptr;
	const byte_t *val_ptr = nullptr;
	size_t key_len = 0;
	size_t val_len = 0;

	uint16_t base;
	uint16_t end;
	size_t k;

	DumpBTHeader();

	base = bt->keys_offs + bt->keys_len + 0x38;
	end = ((hdr->type & 0xFFFF) == 2) ? (m_blocksize - sizeof(APFS_BTFooter)) : m_blocksize;

#if 0
	if (bt->unk_30 != 0x0000 && bt->unk_32 != 0xFFFF && bt->unk_32 != 0x0000)
	{
		m_os << "Entry-30:" << endl;
		DumpHex(m_block + base + bt->unk_30, bt->unk_32);
	}

	if (bt->unk_34 != 0x0000 && bt->unk_36 != 0xFFFF && bt->unk_36 != 0x0000)
	{
		m_os << "Entry-34:" << endl;
		DumpHex(m_block + end - bt->unk_34, bt->unk_36);
	}
#endif

	if (bt->flags & 4)
	{
		if (keys_size == 0 || values_size == 0)
		{
			m_os << "!!! UNKNOWN FIXED KEY / VALUE SIZE !!!" << endl << endl;
			return;
		}

		const APFS_BTEntryFixed * const entry = reinterpret_cast<const APFS_BTEntryFixed *>(m_block + (bt->keys_offs + 0x38));

		for (k = 0; k < bt->entries_cnt; k++)
		{
			assert(entry[k].key_offs != 0xFFFF);

			if (entry[k].key_offs == 0xFFFF)
				continue;

			key_ptr = m_block + base + entry[k].key_offs;
			key_len = keys_size;
			val_ptr = (entry[k].value_offs == 0xFFFF) ? nullptr : m_block + end - entry[k].value_offs;
			val_len = is_index ? 8 : values_size;

			(*this.*func)(key_ptr, key_len, val_ptr, val_len, is_index);
		}
	}
	else
	{
		const APFS_BTEntry * const entry = reinterpret_cast<const APFS_BTEntry *>(m_block + (bt->keys_offs + 0x38));

		for (k = 0; k < bt->entries_cnt; k++)
		{
			assert(entry[k].key_offs != 0xFFFF);

			if (entry[k].key_offs == 0xFFFF)
				continue;

			key_ptr = m_block + base + entry[k].key_offs;
			key_len = entry[k].key_len;
			val_ptr = (entry[k].value_offs == 0xFFFF) ? nullptr : m_block + end - entry[k].value_offs;
			val_len = entry[k].value_len;

			(*this.*func)(key_ptr, key_len, val_ptr, val_len, is_index);
		}
	}

	m_os << endl;

	if ((hdr->type & 0xFFFF) == 2)
		DumpBTFooter();
}

void BlockDumper::DumpBTHeader(bool dump_offsets)
{
	const APFS_BTHeader *bt = reinterpret_cast<const APFS_BTHeader *>(m_block + 0x20);

	m_os << "Flgs | Levl | Entries  | Keys Area   | Free Area   | ? Area      | ? Area" << endl;
	m_os << "-----+------+----------+-------------+-------------+-------------+------------" << endl;

	m_os << setw(4) << bt->flags << " | ";
	m_os << setw(4) << bt->level << " | ";
	m_os << setw(8) << bt->entries_cnt << " | ";
	m_os << setw(4) << bt->keys_offs << " L " << setw(4) << bt->keys_len << " | ";
	m_os << setw(4) << bt->free_offs << " L " << setw(4) << bt->free_len << " | ";
	m_os << setw(4) << bt->unk_30 << " L " << setw(4) << bt->unk_32 << " | ";
	m_os << setw(4) << bt->unk_34 << " L " << setw(4) << bt->unk_36 << endl;
	m_os << endl;

	if (dump_offsets)
	{
		m_os << "Index:  " << setw(4) << bt->keys_offs << " L " << setw(4) << bt->keys_len;
		m_os << " [" << setw(4) << (0x38 + bt->keys_offs) << " - " << setw(4) << (0x38 + bt->keys_offs + bt->keys_len) << "]" << endl;
		m_os << "Free:   " << setw(4) << bt->free_offs << " L " << setw(4) << bt->free_len;
		m_os << " [" << setw(4) << (0x38 + bt->keys_offs + bt->keys_len + bt->free_offs) << " - " << setw(4) << (0x38 + bt->keys_offs + bt->keys_len + bt->free_offs + bt->free_len) << "]" << endl;
		m_os << "Unk1:   " << setw(4) << bt->unk_30 << " L " << setw(4) << bt->unk_32 << endl;
		m_os << "Unk2:   " << setw(4) << bt->unk_34 << " L " << setw(4) << bt->unk_36 << endl;
		m_os << endl;

		size_t cnt;
		size_t k;

		if (bt->flags & 4)
		{
			const APFS_BTEntryFixed *e = reinterpret_cast<const APFS_BTEntryFixed *>(m_block + 0x38);

			cnt = bt->keys_len / 4;

			for (k = 0; k < bt->entries_cnt; k++)
				m_os << setw(2) << k << " : " << setw(4) << e[k].key_offs << " => " << setw(4) << e[k].value_offs << endl;
			m_os << "-----------------" << endl;
			for (; k < cnt; k++)
			{
				if (e[k].key_offs != 0 || e[k].value_offs != 0)
				{
					m_os << setw(2) << k << " : " << setw(4) << e[k].key_offs << " => " << setw(4) << e[k].value_offs << endl;
				}
			}
		}
		else
		{
			const APFS_BTEntry *e = reinterpret_cast<const APFS_BTEntry *>(m_block + 0x38);

			cnt = bt->keys_len / 8;

			for (k = 0; k < bt->entries_cnt; k++)
			{
				m_os << setw(2) << k << " : ";
				m_os << setw(4) << e[k].key_offs << " L " << setw(4) << e[k].key_len << " => ";
				m_os << setw(4) << e[k].value_offs << " L " << setw(4) << e[k].value_len << endl;
			}
			m_os << "-------------------------------" << endl;
			for (; k < cnt; k++)
			{
				if (e[k].key_offs != 0 || e[k].key_len != 0 || e[k].value_offs != 0 || e[k].value_len != 0)
				{
					m_os << setw(2) << k << " : ";
					m_os << setw(4) << e[k].key_offs << " L " << setw(4) << e[k].key_len << " => ";
					m_os << setw(4) << e[k].value_offs << " L " << setw(4) << e[k].value_len << endl;
				}
			}
		}

		m_os << endl;
	}
}

void BlockDumper::DumpBTFooter()
{
	const APFS_BTFooter * const tail = reinterpret_cast<const APFS_BTFooter *>(m_block + 0xFD8);

	m_os << endl;
	m_os << "Unk-FD8  | Unk-FE0  | MinKeySz | MinValSz | MaxKeySz | MaxValSz | Entries          | Nodes" << endl;
	m_os << "---------+----------+----------+----------+----------+----------+------------------+-----------------" << endl;
	m_os << setw(8) << tail->unk_FD8 << " | ";
	m_os << setw(8) << tail->unk_FDC << " | ";
	m_os << setw(8) << tail->min_key_size << " | ";
	m_os << setw(8) << tail->min_val_size << " | ";
	m_os << setw(8) << tail->max_key_size << " | ";
	m_os << setw(8) << tail->max_val_size << " | ";
	m_os << setw(16) << tail->entries_cnt << " | ";
	m_os << setw(16) << tail->nodes_cnt << endl;
}

void BlockDumper::DumpTableHeader(const APFS_TableHeader &tbl)
{
	m_os << "Page | ???? | Entries" << endl;
	m_os << "-----+------+---------" << endl;

	m_os << setw(4) << tbl.page << " | ";
	m_os << setw(4) << tbl.level << " | ";
	m_os << setw(8) << tbl.entries_cnt << endl;

	m_os << endl;
}

void BlockDumper::DumpBTEntry_0_E(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index)
{
	uint64_t key;
	// uint16_t nlen;
	uint8_t type;

	// assert(key_length >= 8);

	if (key_length < 8)
	{
		m_os << "KEY LENGTH TOO SHORT : " << key_length << endl;
		DumpHex(key_data, key_length);
		DumpHex(value_data, value_length);
		return;
	}

	key = *reinterpret_cast<const uint64_t *>(key_data);
	type = key >> 60;
	key &= 0x0FFFFFFFFFFFFFFFULL;

	// m_os << dec;

	// if (type != 4)
	//	return;

	switch (type)
	{
	case 0x3:
		assert(key_length == 8);
		m_os << "File " << key << " => ";

		if (index)
		{
			assert(value_length == 8);
			m_os << hex << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			assert(value_length >= sizeof(APFS_Inode));

			// m_os << "[" << setw(4) << value_length << "] ";

			if (value_length >= sizeof(APFS_Inode))
			{
				const APFS_Inode *obj = reinterpret_cast<const APFS_Inode *>(value_data);

				m_os << obj->parent_id << " ";
				m_os << obj->object_id << " ";
#if 0
				m_os << obj->birthtime << " ";
				m_os << obj->mtime << " ";
				m_os << obj->ctime << " ";
				m_os << obj->atime << " ";
#else
				m_os << "[TS] ";
#endif
				m_os << obj->unk_30 << " " << obj->refcnt << " ";
				m_os << obj->unk_40 << " " << obj->flags << " " << obj->uid << " " << obj->gid << " ";
				m_os << oct << obj->mode << hex << " ";
				m_os << obj->unk_58 << " " << obj->entries_cnt << " " << obj->entries_len << " : ";

				const APFS_InodeEntry *entry = reinterpret_cast<const APFS_InodeEntry *>(value_data + 0x60);
				uint16_t entry_base = 0x60 + (obj->entries_cnt * sizeof(APFS_InodeEntry));
				uint16_t k;

				for (k = 0; k < obj->entries_cnt; k++)
				{
					m_os << setw(4) << entry[k].type << " ";
					m_os << setw(4) << entry[k].len << " : ";

					// DumpHex(value_data + entry_base, entry[k].len, entry[k].len);

				}

				for (k = 0; k < obj->entries_cnt; k++)
				{
					switch (entry[k].type)
					{
					case 0x0204:
						m_os << '\'' << (value_data + entry_base) << '\'';
						break;

					case 0x2008:
					{
						const APFS_Inode_Sizes *ft = reinterpret_cast<const APFS_Inode_Sizes *>(value_data + entry_base);
						m_os << ft->size << " " << ft->size_on_disk << " " << ft->unk_10 << " " << ft->size_2 << " " << ft->unk_20 << " ";

						if (ft->size != ft->size_2)
							m_os << "[???] ";

						break;
					}

					case 0x280D:
						m_os << *reinterpret_cast<const uint64_t *>(value_data + entry_base);
						break;

					default:
						m_os << "[!!!] ";
						DumpHex(value_data + entry_base, entry[k].len, entry[k].len);
					}

					entry_base += ((entry[k].len + 7) & 0xFFF8);

					if (k < (obj->entries_cnt - 1))
						m_os << " : ";
				}

				m_os << endl;
			}
			else
			{
				m_os << "[!!!]" << endl;
				DumpHex(value_data, value_length);
			}
		}

		break;

	case 0x4:
		assert(key_length >= 10);
		m_os << "Attr " << key << " ";
		// nlen = *reinterpret_cast<const uint16_t *>(key_data + 8);
		m_os << '\'' << (key_data + 10) << '\'';

		if (index)
		{
			assert(value_length == 8);
			m_os << " => " << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			const APFS_Attribute *attr = reinterpret_cast<const APFS_Attribute *>(value_data);

			assert(attr->size + 4 == value_length);

			m_os << " => " << attr->type << " " << attr->size;

			if (attr->type == 1 && attr->size == 0x30)
			{
				const APFS_AttributeLink *attrlnk = reinterpret_cast<const APFS_AttributeLink *>(value_data + 4);

				m_os << " : " << attrlnk->object_id << " " << attrlnk->size << " " << attrlnk->size_on_disk << " ";
				m_os << attrlnk->unk[0] << " " << attrlnk->unk[1] << " " << attrlnk->unk[2] << endl;
			}
			else
			{
				const char *attr_name = reinterpret_cast<const char *>(key_data + 10);

				if (attr->type != 2 && attr->type != 6)
					m_os << " [!?!] ";

				if (!strcmp(attr_name, "com.apple.fs.symlink"))
					m_os << " : " << '\'' << (value_data + 4) << '\'' << endl;
				else if (!strcmp(attr_name, "com.apple.quarantine"))
				{
					// Laenge begrenzen ...
					std::string str(reinterpret_cast<const char *>(value_data + 4), value_length - 4);
					m_os << " : '" << str << '\'' << endl;
				}
				else
				{
					m_os << endl;
					DumpHex(value_data + 4, value_length - 4);
				}
			}

			// if (strcmp(reinterpret_cast<const char *>(key_data + 10), "com.apple.decmpfs"))
		}

		break;

	case 0x5:
		assert(key_length == 0x10);
		m_os << "HLnk " << key << " ";
		m_os << *reinterpret_cast<const uint64_t *>(key_data + 8) << " => ";

		if (index)
		{
			assert(value_length == 8);
			m_os << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			struct Val_50
			{
				uint64_t id;
				uint16_t namelen;
			};

			const Val_50 * const v = reinterpret_cast<const Val_50 *>(value_data);

			m_os << v->id << " ";
			m_os << (value_data + 0xA);
			m_os << endl;
		}
		break;

	case 0x6:
		assert(key_length == 8);
		m_os << "RefC " << key << " => ";
		if (index)
		{
			assert(value_length == 8);
			m_os << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			assert(value_length == 4);
			m_os << *reinterpret_cast<const uint32_t *>(value_data) << endl;

			if (value_length != 4)
				DumpHex(value_data, value_length);
		}

		break;

	case 0x8:
		assert(key_length == 16);
		m_os << "Data " << key << " " << *reinterpret_cast<const uint64_t *>(key_data + 8) << " => ";
		// key | offset : len | block | crypto_id

		m_os << hex;

		if (index)
		{
			assert(value_length == 8);
			m_os << hex << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			assert(value_length == 0x18);

			if (value_length == 0x18)
			{
				const APFS_Extent *ext = reinterpret_cast<const APFS_Extent *>(value_data);
				m_os << ext->size << " " << ext->block << " " << ext->crypto_id << endl;
			}
			else
			{
				DumpHex(value_data, value_length);
			}
		}

		break;

	case 0x9:
		assert(key_length >= 10);

		// DumpHex(key_data, key_length, key_length);

		m_os << "Name " << key << " ";

		/*
		nlen = *reinterpret_cast<const uint16_t *>(key_data + 8);
		for (size_t k = 0; k < nlen; k++)
		m_os << data[10 + k];
		*/

		if (m_text_flags & 0x09)
		{
			uint32_t hash_stored = *reinterpret_cast<const uint32_t *>(key_data + 8);

			m_os << setw(8) << hash_stored << " ";

#if 1 // Hash verify
			uint32_t hash_calc = HashFilename(reinterpret_cast<const char *>(key_data + 12), hash_stored & 0x3FF, (m_text_flags & 0x09) == 0x01);
			m_os << "[" << setw(8) << hash_calc << "] ";

			if (hash_calc != hash_stored)
				cerr << hex << "Hash not matching at name " << key << " : stored " << hash_stored << ", calc " << hash_calc << endl;
#endif

			m_os << '\'' << (key_data + 12) << '\'';
		}
		else
			m_os << '\'' << (key_data + 10) << '\'';
		m_os << " => ";

		if (index)
		{
			assert(value_length == 8);
			m_os << hex << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			if (value_length == 0x12)
			{
				const APFS_Name *ptr = reinterpret_cast<const APFS_Name *>(value_data);
				m_os << ptr->id << " [" << tstamp(ptr->timestamp) << "] " << ptr->unk << endl;
			}
			else if (value_length == 0x22)
			{
				const APFS_Name *ptr = reinterpret_cast<const APFS_Name *>(value_data);
				const APFS_Name_SLink *sptr = reinterpret_cast<const APFS_Name_SLink *>(value_data + 0x12);
				m_os << ptr->id << " [" << tstamp(ptr->timestamp) << "] " << ptr->unk << " [???] ";
				m_os << sptr->unk_12 << " " << sptr->unk_14 << " " << (int)sptr->unk_16 << " " << (int)sptr->unk_17 << " " << sptr->unk_18 << " " << sptr->obj_id << endl;
			}
			else
			{
				DumpHex(value_data, value_length);
			}
		}

		break;

	case 0xC:
		m_os << "HLk2 " << key << " => " << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		break;

	default:
		m_os << "KEY TYPE UNKNOWN" << endl;

		m_os << hex;
		DumpHex(key_data, key_length);
		DumpHex(value_data, value_length);
		break;
	}

	m_os << hex;
}

void BlockDumper::DumpBTEntry_4_B(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;

	const APFS_Key_B_NodeID_Map *ekey = reinterpret_cast<const APFS_Key_B_NodeID_Map *>(key_ptr);

	m_os << setw(16) << ekey->nodeid << " | ";
	m_os << setw(16) << ekey->version << " => ";

	if (index)
	{
		m_os << setw(16) << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
	}
	else
	{
		if (!val_ptr)
		{
			m_os << "-------- | -------- | ----------------" << endl;
		}
		else
		{
			const APFS_Value_B_NodeID_Map *val = reinterpret_cast<const APFS_Value_B_NodeID_Map *>(val_ptr);

			m_os << setw(8) << val->flags << " | ";
			m_os << setw(8) << val->size << " | ";
			m_os << setw(16) << val->blockid << endl;
		}
	}
}

void BlockDumper::DumpBTEntry_4_F(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;

	assert(key_length == 8);
	uint64_t key = *reinterpret_cast<const uint64_t *>(key_ptr);

	m_os << hex << setw(16) << key << " => ";

	if (index)
	{
		assert(value_length == 8);
		m_os << setw(16) << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
	}
	else
	{
		if (!val_ptr)
		{
			m_os << "---------------- | ---------------- | --------" << endl;
		}
		else
		{
			assert(value_length == sizeof(APFS_Value_F));
			const APFS_Value_F *val = reinterpret_cast<const APFS_Value_F *>(val_ptr);

			m_os << setw(16) << val->block_cnt << " | ";
			m_os << setw(16) << val->obj_id << " | ";
			m_os << setw(8) << val->unk_10 << endl;
		}
	}
}

void BlockDumper::DumpBTEntry_4_10(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;

	uint64_t key = *reinterpret_cast<const uint64_t *>(key_ptr);

	switch (key >> 60)
	{
	case 0x1:
		m_os << setw(16) << key;
		break;
	case 0xB:
		m_os << setw(16) << key;
		m_os << " " << (key_ptr + 0xA);
	}

	m_os << " => ";

	if (!val_ptr)
	{
		m_os << "--------" << endl;
	}
	else
	{
		if (index)
		{
			assert(value_length == 8);
			m_os << setw(16) << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
		}
		else
		{
			switch (key >> 60)
			{
			case 0x1:
			{
				const APFS_Value_10_1 *val = reinterpret_cast<const APFS_Value_10_1 *>(val_ptr);
				m_os << setw(16) << val->unk_00 << " ";
				m_os << setw(16) << val->unk_08 << " ";
				// m_os << setw(16) << val->tstamp_10 << " ";
				// m_os << setw(16) << val->tstamp_18 << " ";
				m_os << "[" << tstamp(val->tstamp_10) << "] ";
				m_os << "[" << tstamp(val->tstamp_18) << "] ";
				m_os << setw(16) << val->unk_20 << " ";
				m_os << setw(8) << val->type_28 << " ";
				m_os << setw(8) << val->unk_2C << " ";
				m_os << (val_ptr + 0x32);
				break;
			}
			case 0xB:
			{
				const APFS_Value_10_B *val = reinterpret_cast<const APFS_Value_10_B *>(val_ptr);
				m_os << setw(16) << val->id;
				break;
			}
			}
		}
	}
	m_os << endl;
}

void BlockDumper::DumpBTEntry_4_13(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;
	(void)index;

	DumpHex(key_ptr, 0x08);
	DumpHex(val_ptr, 0x10);
	m_os << endl;
}

void BlockDumper::DumpBTEntry_8_9(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;
	(void)index;

	const APFS_Key_8_9 *key = reinterpret_cast<const APFS_Key_8_9 *>(key_ptr);

	assert(key_length == 0x10);
	assert(value_length == 0x08);

	m_os << setw(16) << key->version << " | ";
	m_os << setw(16) << key->blk_id << " => ";

	if (!val_ptr)
		m_os << "----------------" << endl;
	else
		m_os << setw(16) << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
}

void BlockDumper::DumpBTEntry_Unk(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)index;

	m_os << "Key: " << std::endl;
	DumpHex(key_ptr, key_length);
	m_os << "Value: " << std::endl;
	DumpHex(val_ptr, value_length);
	m_os << std::endl;
}

void BlockDumper::DumpBlk_0_D()
{
	const APFS_Superblock_APSB * const sb = reinterpret_cast<const APFS_Superblock_APSB *>(m_block);
	int k;

	m_os << hex;
	m_os << "Signature        : " << setw(8) << sb->signature << endl;
	m_os << "Unknown 0x24     : " << setw(8) << sb->unk_24 << endl;
	m_os << "Features 0x28    : " << setw(16) << sb->features_28 << endl;
	m_os << "Features 0x30    : " << setw(16) << sb->features_30 << endl;
	m_os << "Features 0x38    : " << setw(16) << sb->features_38 << endl;
	m_os << "Unknown 0x40     : " << setw(16) << sb->unk_40 << endl;
	m_os << "Blocks Reserved  : " << setw(16) << sb->blocks_reserved << endl;
	m_os << "Blocks Maximum   : " << setw(16) << sb->blocks_quota << endl;
	m_os << "Unknown 0x58     : " << setw(16) << sb->unk_58 << endl;
	m_os << "Unknown 0x60     : " << setw(16) << sb->unk_60 << endl;
	m_os << "Unknown 0x68     : " << setw(8) << sb->unk_68 << endl;
	m_os << "Unknown 0x6C     : " << setw(8) << sb->unk_6C << endl;
	m_os << "Unknown 0x70     : " << setw(8) << sb->unk_70 << endl;
	m_os << "Unknown 0x74     : " << setw(8) << sb->unk_74 << endl;
	m_os << "Unknown 0x78     : " << setw(8) << sb->unk_78 << endl;
	m_os << "Unknown 0x7C     : " << setw(8) << sb->unk_7C << endl;
	m_os << "Block-ID NodeMap : " << setw(16) << sb->blockid_nodemap << endl;
	m_os << "Node-ID Root Dir : " << setw(16) << sb->nodeid_rootdir << endl;
	m_os << "Block-ID BlkMap  : " << setw(16) << sb->blockid_blockmap << endl;
	m_os << "Block-ID 4/10    : " << setw(16) << sb->blockid_4xBx10_map << endl;
	m_os << "Unknown 0xA0     : " << setw(16) << sb->unk_A0 << endl;
	m_os << "Unknown 0xA8     : " << setw(16) << sb->unk_A8 << endl;
	m_os << "Unknown 0xB0     : " << setw(16) << sb->unk_B0 << endl;
	m_os << "Unknown 0xB8     : " << setw(16) << sb->unk_B8 << endl;
	m_os << "Unknown 0xC0     : " << setw(16) << sb->unk_C0 << endl;
	m_os << "Unknown 0xC8     : " << setw(16) << sb->unk_C8 << endl;
	m_os << "Unknown 0xD0     : " << setw(16) << sb->unk_D0 << endl;
	m_os << "Unknown 0xD8     : " << setw(16) << sb->unk_D8 << endl;
	m_os << "Unknown 0xE0     : " << setw(16) << sb->unk_E0 << endl;
	m_os << "Unknown 0xE8     : " << setw(16) << sb->unk_E8 << endl;
	m_os << "GUID             : " << uuid(sb->guid) << endl;
	m_os << "Timestamp 0x100  : " << tstamp(sb->timestamp_100) << endl;
	m_os << "Version   0x108  : " << setw(16) << sb->flags_108 << endl;
	for (k = 0; k < 9; k++)
	{
		m_os << "Accessor Info    : " << sb->access_info[k].accessor << endl;
		m_os << "Timestamp        : " << tstamp(sb->access_info[k].timestamp) << endl;
		m_os << "Version          : " << setw(16) << sb->access_info[k].version << endl;
	}
	m_os << "Volume Name      : " << sb->vol_name << endl;
	m_os << "Unknown 0x3C0    : " << setw(16) << sb->unk_3C0 << endl;
	m_os << "Unknown 0x3C8    : " << setw(16) << sb->unk_3C8 << endl;

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_4_7()
{
	const APFS_Block_4_7_Bitmaps * const blk = reinterpret_cast<const APFS_Block_4_7_Bitmaps *>(m_block);

	size_t k;

	DumpTableHeader(blk->tbl);

	m_os << "Version          | Offset           | Bits Tot | Bits Avl | Block" << endl;
	m_os << "-----------------+------------------+----------+----------+-----------------" << endl;

	for (k = 0; k < blk->tbl.entries_cnt; k++)
	{
		m_os << setw(16) << blk->bmp[k].version << " | ";
		m_os << setw(16) << blk->bmp[k].offset << " | ";
		m_os << setw(8) << blk->bmp[k].bits_total << " | ";
		m_os << setw(8) << blk->bmp[k].bits_avail << " | ";
		m_os << setw(16) << blk->bmp[k].block << endl;
	}
}

void BlockDumper::DumpBlk_4_B()
{
	const APFS_Block_4_B_BTreeRootPtr * const blk = reinterpret_cast<const APFS_Block_4_B_BTreeRootPtr *>(m_block);

	DumpTableHeader(blk->tbl);

	m_os << "Type 1   | Type 2   | Block" << endl;
	m_os << "---------+----------+-----------------" << endl;

	m_os << setw(8) << blk->entry[0].type_1 << " | ";
	m_os << setw(8) << blk->entry[0].type_2 << " | ";
	m_os << setw(16) << blk->entry[0].blk << endl;
	m_os << endl;

	if (!IsZero(m_block + 0x38, 0xFC8))
	{
		m_os << "!!! ADDITIONAL DATA !!!" << endl;
		DumpBlockHex();
	}
}

void BlockDumper::DumpBlk_4_C()
{
	const APFS_Block_4_C * const blk = reinterpret_cast<const APFS_Block_4_C *>(m_block);

	size_t k;

	DumpTableHeader(blk->tbl);

	m_os << "Typ      | Subtype  | Unk-08           | Unk-10           | Node-ID          | Block" << endl;
	m_os << "---------+----------+------------------+------------------+------------------+-----------------" << endl;

	for (k = 0; k < blk->tbl.entries_cnt; k++)
	{
		m_os << setw(8) << blk->entry[k].type << " | ";
		m_os << setw(8) << blk->entry[k].subtype << " | ";
		m_os << setw(16) << blk->entry[k].unk_08 << " | ";
		m_os << setw(16) << blk->entry[k].unk_10 << " | ";
		m_os << setw(16) << blk->entry[k].nodeid << " | ";
		m_os << setw(16) << blk->entry[k].block << endl;
	}
}

void BlockDumper::DumpBlk_8_1()
{
	const APFS_Superblock_NXSB * const sb = reinterpret_cast<const APFS_Superblock_NXSB *>(m_block);

	m_os << hex;
	m_os << "Signature        : " << setw(8) << sb->signature << endl;
	m_os << "Block Size       : " << setw(8) << sb->block_size << endl;
	m_os << "Block Count      : " << setw(16) << sb->block_count << endl;
	m_os << "Unknown 0x30     : " << setw(16) << sb->unk_30 << endl;
	m_os << "Unknown 0x38     : " << setw(16) << sb->unk_38 << endl;
	m_os << "Unknown 0x40     : " << setw(16) << sb->unk_40 << endl;
	m_os << "GUID             : " << setw(16) << uuid(sb->container_guid) << endl;
	m_os << "Next Node ID     : " << setw(16) << sb->next_nodeid << endl;
	m_os << "Next Version     : " << setw(16) << sb->next_version << endl;
	m_os << "No of NXSB & 4_C : " << setw(8) << sb->sb_area_cnt << endl;
	m_os << "No of other Hdr  : " << setw(8) << sb->spaceman_area_cnt << endl;
	m_os << "Block-ID SB Area : " << setw(16) << sb->blockid_sb_area_start << endl;
	m_os << "Block-ID SM Area : " << setw(16) << sb->blockid_spaceman_area_start << endl;
	m_os << "Next NXSB        : " << setw(8) << sb->next_sb << endl;
	m_os << "Next SpMgr       : " << setw(8) << sb->next_spaceman << endl;
	m_os << "Curr SB Start    : " << setw(8) << sb->current_sb_start << endl;
	m_os << "Curr SB Len      : " << setw(8) << sb->current_sb_len << endl;
	m_os << "Curr SM Start    : " << setw(8) << sb->current_spaceman_start << endl;
	m_os << "Curr SM Len      : " << setw(8) << sb->current_spaceman_len << endl;
	m_os << "Node-ID SM Hdr   : " << setw(16) << sb->nodeid_8x5 << endl;
	m_os << "Block-ID Vol-Hdr : " << setw(16) << sb->blockid_volhdr << endl;
	m_os << "Node-ID 8-11 Hdr : " << setw(16) << sb->nodeid_8x11 << endl;
	m_os << "Unknown 0xB0     : " << setw(8) << sb->unk_B0 << endl;
	m_os << "Unknown 0xB4     : " << setw(8) << sb->unk_B4 << endl;

	for (size_t k = 0; (k < 100) && (sb->nodeid_apsb[k] != 0); k++)
		m_os << "Node-ID APSB " << setw(2) << k << "  : " << setw(16) << sb->nodeid_apsb[k] << endl;

	m_os << endl;

	m_os << "Block-ID Keybag  : " << setw(16) << sb->keybag_blk_start << endl;
	m_os << "Block-Cnt Keybag : " << setw(16) << sb->keybag_blk_count << endl;

	m_os << endl;

	DumpBlockHex();
}

void BlockDumper::DumpBlk_8_5()
{
	const APFS_Block_8_5_Spaceman *b = reinterpret_cast<const APFS_Block_8_5_Spaceman *>(m_block);

	m_os << hex;
	m_os << "0020             : " << setw(8) << b->unk_20 << endl;
	m_os << "0024             : " << setw(8) << b->unk_24 << endl;
	m_os << "0028             : " << setw(8) << b->unk_28 << endl;
	m_os << "002C             : " << setw(8) << b->unk_2C << endl;
	m_os << "0030 Blk Tot     : " << setw(16) << b->blocks_total << endl;
	m_os << "0038             : " << setw(16) << b->unk_38 << endl;
	m_os << "0040             : " << setw(16) << b->unk_40 << endl;
	m_os << "0048 Blk Free    : " << setw(16) << b->blocks_free << endl;
	m_os << "0050             : " << setw(16) << b->unk_50 << endl;
	m_os << "0058             : " << setw(16) << b->unk_58 << endl;
	m_os << "0060             : " << setw(16) << b->unk_60 << endl;
	m_os << "0068             : " << setw(16) << b->unk_68 << endl;
	m_os << "0070             : " << setw(16) << b->unk_70 << endl;
	m_os << "0078             : " << setw(16) << b->unk_78 << endl;
	m_os << "0080             : " << setw(16) << b->unk_80 << endl;
	m_os << "0088             : " << setw(16) << b->unk_88 << endl;
	m_os << "0090             : " << setw(8) << b->unk_90 << endl;
	m_os << "0094             : " << setw(8) << b->unk_94 << endl;
	m_os << "0098 BCnt VolBmp : " << setw(16) << b->blockcnt_bitmaps_98 << endl;
	m_os << "00A0             : " << setw(8) << b->unk_A0 << endl;
	m_os << "00A4 BCnt MgrBmp : " << setw(8) << b->blockcnt_bitmaps_A4 << endl;
	m_os << "00A8 BID MgrBmp  : " << setw(16) << b->blockid_begin_bitmaps_A8 << endl;
	m_os << "00B0 BID VolBmp  : " << setw(16) << b->blockid_bitmaps_B0 << endl;
	m_os << "00B8             : " << setw(16) << b->unk_B8 << endl;
	m_os << "Spaceman" << endl;
	m_os << "00C0             : " << setw(16) << b->unk_C0 << endl;
	m_os << "00C8             : " << setw(16) << b->unk_C8 << endl;
	m_os << "00D0 NID Obs 1   : " << setw(16) << b->nodeid_obsolete_1 << endl;
	m_os << "00D8             : " << setw(16) << b->unk_D8 << endl;
	m_os << "00E0             : " << setw(16) << b->unk_E0 << endl;
	m_os << "00E8             : " << setw(16) << b->unk_E8 << endl;
	m_os << "00F0             : " << setw(16) << b->unk_F0 << endl;
	m_os << "00F8 NID Obs 2   : " << setw(16) << b->nodeid_obsolete_2 << endl;
	m_os << "0100             : " << setw(16) << b->unk_100 << endl;
	m_os << "0108             : " << setw(16) << b->unk_108 << endl;
	m_os << "0110             : " << setw(16) << b->unk_110 << endl;
	m_os << "0118             : " << setw(16) << b->unk_118 << endl;
	m_os << "0120             : " << setw(16) << b->unk_120 << endl;
	m_os << "0128             : " << setw(16) << b->unk_128 << endl;
	m_os << "0130             : " << setw(16) << b->unk_130 << endl;
	m_os << "0138             : " << setw(16) << b->unk_138 << endl;
	m_os << "0140             : " << setw(16) << b->unk_140 << endl;
	m_os << "0142             : " << setw(16) << b->unk_142 << endl;
	m_os << "0144             : " << setw(16) << b->unk_144 << endl;
	m_os << "0148             : " << setw(16) << b->unk_148 << endl;
	m_os << "014C             : " << setw(16) << b->unk_14C << endl;
	m_os << "0150             : " << setw(16) << b->unk_150 << endl;
	m_os << "0158             : " << setw(16) << b->unk_158 << endl;
	for (size_t k = 0; k < 10; k++)
		m_os << setw(4) << (0x160 + 2 * k) << "             : " << setw(4) << b->unk_160[k] << endl;
	m_os << "0180 BID BmpHdr  : " << setw(16) << b->blockid_vol_bitmap_hdr << endl;

	DumpBlockHex();
}

void BlockDumper::DumpBlk_8_11()
{
	DumpBlockHex();
}

void BlockDumper::DumpBTNode_0()
{
	const APFS_BlockHeader * const hdr = reinterpret_cast<const APFS_BlockHeader *>(m_block);

	switch (hdr->subtype)
	{
	case 0x0E:
		DumpBTNode(&BlockDumper::DumpBTEntry_0_E);
		break;

	default:
		DumpBTNode(&BlockDumper::DumpBTEntry_Unk);
		break;
	}

	// DumpHex(m_block, m_blocksize, 0x20);
}

void BlockDumper::DumpBTNode_4()
{
	const APFS_BlockHeader * const hdr = reinterpret_cast<const APFS_BlockHeader *>(m_block);

	switch (hdr->subtype)
	{
	case 0xB:
		DumpBTNode(&BlockDumper::DumpBTEntry_4_B, 0x10, 0x10);
		break;

	case 0xF:
		DumpBTNode(&BlockDumper::DumpBTEntry_4_F);
		break;

	case 0x10:
		DumpBTNode(&BlockDumper::DumpBTEntry_4_10);
		break;

	case 0x13:
		DumpBTNode(&BlockDumper::DumpBTEntry_4_13, 0x8, 0x10);
		break;

	default:
		DumpBTNode(&BlockDumper::DumpBTEntry_Unk);
		break;
	}
}

void BlockDumper::DumpBTNode_8()
{
	const APFS_BlockHeader * const hdr = reinterpret_cast<const APFS_BlockHeader *>(m_block);

	switch (hdr->subtype)
	{
	case 0x09:
		DumpBTNode(&BlockDumper::DumpBTEntry_8_9, 0x10, 0x08);
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

void BlockDumper::DumpHex(const byte_t * data, size_t size, size_t line_size)
{
	::DumpHex(m_os, data, size, line_size);
}

const char * BlockDumper::GetNodeType(uint32_t type, uint32_t subtype)
{
	const char *typestr = "Unknown";

	switch (type)
	{
	case 0x00000002:
	case 0x00000003:
		switch (subtype)
		{
		case 0x0000000E:
			typestr = "Directory";
			break;
		}
		break;
	case 0x0000000D:
		typestr = "Volume Superblock (APSB)";
		break;
	case 0x40000002:
	case 0x40000003:
		switch (subtype)
		{
		case 0x0000000B:
			typestr = "Mapping Node-ID => Block-ID";
			break;
		case 0x0000000F:
			typestr = "Mapping Block-ID => Object-ID";
			break;
		case 0x00000010:
			typestr = "Snapshot-Info";
			break;
		case 0x00000013:
			typestr = "Unknown B*-Tree";
			break;
		}
		break;
	case 0x40000007:
		typestr = "Bitmap Header (4/7)";
		break;
	case 0x4000000B:
		typestr = "Some Pointer";
		break;
	case 0x4000000C:
		typestr = "ID Mapping";
		break;
	case 0x80000001:
		typestr = "Container Superblock (NXSB)";
		break;
	case 0x80000002:
	case 0x80000003:
		switch (subtype)
		{
		case 0x00000009:
			typestr = "Deletable Blocks B*-Tree (?)";
			break;
		}
		break;
	case 0x80000005:
		typestr = "Spaceman Header";
		break;
	case 0x80000011:
		typestr = "Unknown Header";
		break;
	}

	return typestr;
}

const std::string BlockDumper::tstamp(uint64_t apfs_time)
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

const std::string BlockDumper::uuid(const apfs_uuid_t &uuid)
{
	stringstream st;
	int k;

	st << hex << uppercase << setfill('0');
	for (k = 0; k < 4; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 4; k < 6; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 6; k < 8; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 8; k < 10; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 10; k < 16; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);

	return st.str();
}

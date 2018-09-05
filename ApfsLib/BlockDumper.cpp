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

	const APFS_ObjHeader * const node = reinterpret_cast<const APFS_ObjHeader *>(m_block);
	// const APFS_BTHeader * const bt = reinterpret_cast<const APFS_BTHeader *>(m_block + 0x28);

	if (IsEmptyBlock(m_block, m_blocksize))
	{
		m_os << setw(16) << blk_nr << " [Empty]" << endl;
		m_block = nullptr;
		return;
	}

	if (!VerifyBlock(m_block, m_blocksize))
	{
		/*
		m_os << setw(16) << blk_nr << " !!! CHECKSUM ERROR !!!" << endl;
		m_block = nullptr;
		*/
		m_os << setw(16) << blk_nr << ":" << endl;
		DumpBlockHex();
		return;
	}

#if 0
	if (node->type != 0x0000000D)
		return;
#endif

	DumpNodeHeader(node, blk_nr);

	switch (node->type & 0xFFFFFFF)
	{
	case 0x0001: // NXSB Block
		DumpBlk_NXSB();
		break;
	case 0x0002: // BTree Root
	case 0x0003: // BTree Node
		DumpBTNode_0();
		break;
	case 0x0005:
		DumpBlk_SM();
		break;
	case 0x0006:
		DumpBlk_CAB();
		break;
	case 0x0007: // Bitmap Block List
		DumpBlk_CIB();
		break;
	case 0x000B: // Pointer to Header (?)
		DumpBlk_OM();
		break;
	case 0x000C: // Another Mapping
		DumpBlk_CPM();
		break;
	case 0x000D: // APSB Block
		DumpBlk_APSB();
		break;
	case 0x0011:
		DumpBlk_NR();
		break;
	case 0x12:
		DumpBlk_NRL();
		break;

	case 0x14:
		DumpBlk_JSDR();
		break;

	default:
		// assert(false);
		std::cerr << "!!! UNKNOWN NODE TYPE " << hex << setw(8) << node->type << " in block " << setw(16) << blk_nr << " !!!" << endl;
		m_os << "!!! UNKNOWN NODE TYPE !!!" << endl;
		DumpBlockHex();
		break;
	}

	m_os << endl;
	m_os << "===========================================================================================================================" << endl;
	m_os << endl;

	m_block = nullptr;
}

void BlockDumper::DumpNodeHeader(const APFS_ObjHeader *blk, uint64_t blk_nr)
{
	m_os << "[paddr]          | cksum            | oid              | xid              | type     | subtype  | description" << endl;
	m_os << "-----------------+------------------+------------------+------------------+----------+----------+-----------------------" << endl;

	m_os << setw(16) << blk_nr << " | ";
	m_os << setw(16) << blk->cksum << " | ";
	m_os << setw(16) << blk->oid << " | ";
	m_os << setw(16) << blk->xid << " | ";
	m_os << setw(8) << blk->type << " | ";
	m_os << setw(8) << blk->subtype << " | ";
	m_os << GetNodeType(blk->type, blk->subtype) << endl;
	m_os << endl;
}

#undef BT_VERBOSE

void BlockDumper::DumpBTNode(DumpFunc func, uint16_t keys_size, uint16_t values_size)
{
	const APFS_ObjHeader * const hdr = reinterpret_cast<const APFS_ObjHeader *>(m_block);
	const APFS_BTHeader * const bt = reinterpret_cast<const APFS_BTHeader *>(m_block + 0x20);
	const bool is_index = bt->level > 0;

	const byte_t *key_ptr = nullptr;
	const byte_t *val_ptr = nullptr;
	size_t key_len = 0;
	size_t val_len = 0;

	uint16_t base;
	uint16_t end;
	size_t k;

#ifdef BT_VERBOSE
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

	base = bt->table_space_offset + bt->table_space_length + 0x38;
	end = (APFS_OBJ_TYPE(hdr->type) == BlockType_BTRoot) ? (m_blocksize - sizeof(APFS_BTFooter)) : m_blocksize;

#ifdef BT_VERBOSE
	if (bt->key_free_list_space_offset != 0xFFFF)
	{
		m_os << "Key Free List Space (" << setw(4) << (base + bt->key_free_list_space_offset) << ") :" << endl;
		// DumpHex(m_block + base + bt->key_free_list_space_offset, bt->key_free_list_space_length);
		flo = bt->key_free_list_space_offset;
		while (flo != 0xFFFF)
		{
			fle = reinterpret_cast<const FreeListEntry *>(m_block + base + flo);
			m_os << setw(4) << flo << ":" << setw(4) << fle->next_offs << "/" << setw(4) << fle->free_size << endl;
			DumpHex(m_block + base + flo, fle->free_size);
			flo = fle->next_offs;
			tot_key_free += fle->free_size;
		}
	}

	if (bt->val_free_list_space_offset != 0xFFFF)
	{
		m_os << "Val Free List Space (" << setw(4) << (end - bt->val_free_list_space_offset) << ") :" << endl;
		// DumpHex(m_block + end - bt->val_free_list_space_offset, bt->val_free_list_space_length);
		flo = bt->val_free_list_space_offset;
		while (flo != 0xFFFF)
		{
			fle = reinterpret_cast<const FreeListEntry *>(m_block + end - flo);
			m_os << setw(4) << flo << ":" << setw(4) << fle->next_offs << "/" << setw(4) << fle->free_size << endl;
			DumpHex(m_block + end - flo, fle->free_size);
			flo = fle->next_offs;
			tot_val_free += fle->free_size;
		}
	}

	m_os << "Free List: Tot Key = " << tot_key_free << ", Tot Val = " << tot_val_free << endl;
	if (tot_key_free != bt->key_free_list_space_length)
		m_os << "[!!!]" << endl;
	if (tot_val_free != bt->val_free_list_space_length)
		m_os << "[!!!]" << endl;
#endif

	if (bt->flags & 4)
	{
		if (keys_size == 0 || values_size == 0)
		{
			m_os << "!!! UNKNOWN FIXED KEY / VALUE SIZE !!!" << endl << endl;

			if (APFS_OBJ_TYPE(hdr->type) == BlockType_BTRoot)
				DumpBTFooter();

			return;
		}

		const APFS_BTEntryFixed * const entry = reinterpret_cast<const APFS_BTEntryFixed *>(m_block + (bt->table_space_offset + 0x38));

		for (k = 0; k < bt->key_count; k++)
		{
#ifdef BT_VERBOSE
			m_os << endl << k << ": " << setw(4) << entry[k].key_offs << " / " << setw(4) << entry[k].value_offs << endl;
#endif

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
		const APFS_BTEntry * const entry = reinterpret_cast<const APFS_BTEntry *>(m_block + (bt->table_space_offset + 0x38));

		for (k = 0; k < bt->key_count; k++)
		{
#ifdef BT_VERBOSE
			m_os << endl << k << ": " << setw(4) << entry[k].key_offs << " L " << setw(4) << entry[k].key_len;
			m_os << " / " << setw(4) << entry[k].value_offs << " / " << setw(4) << entry[k].value_len << endl;
#endif

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

	if (APFS_OBJ_TYPE(hdr->type) == BlockType_BTRoot)
		DumpBTFooter();
}

void BlockDumper::DumpBTHeader(bool dump_offsets)
{
	const APFS_BTHeader *bt = reinterpret_cast<const APFS_BTHeader *>(m_block + 0x20);

	m_os << "Flgs | Levl | Key Cnt  | Table Area  | Free Area   | Key Free L  | Val Free L" << endl;
	m_os << "-----+------+----------+-------------+-------------+-------------+------------" << endl;

	m_os << setw(4) << bt->flags << " | ";
	m_os << setw(4) << bt->level << " | ";
	m_os << setw(8) << bt->key_count << " | ";
	m_os << setw(4) << bt->table_space_offset << " L " << setw(4) << bt->table_space_length << " | ";
	m_os << setw(4) << bt->free_space_offset << " L " << setw(4) << bt->free_space_length << " | ";
	m_os << setw(4) << bt->key_free_list_space_offset << " L " << setw(4) << bt->key_free_list_space_length << " | ";
	m_os << setw(4) << bt->val_free_list_space_offset << " L " << setw(4) << bt->val_free_list_space_length << endl;
	m_os << endl;

	if (dump_offsets)
	{
		m_os << "Index:  " << setw(4) << bt->table_space_offset << " L " << setw(4) << bt->table_space_length;
		m_os << " [" << setw(4) << (0x38 + bt->table_space_offset) << " - " << setw(4) << (0x38 + bt->table_space_offset + bt->table_space_length) << "]" << endl;
		m_os << "Free:   " << setw(4) << bt->free_space_offset << " L " << setw(4) << bt->free_space_length;
		m_os << " [" << setw(4) << (0x38 + bt->table_space_offset + bt->table_space_length + bt->free_space_offset) << " - " << setw(4) << (0x38 + bt->table_space_offset + bt->table_space_length + bt->free_space_offset + bt->free_space_length) << "]" << endl;
		m_os << "K Free: " << setw(4) << bt->key_free_list_space_offset << " L " << setw(4) << bt->key_free_list_space_length << endl;
		m_os << "V Free: " << setw(4) << bt->val_free_list_space_offset << " L " << setw(4) << bt->val_free_list_space_length << endl;
		m_os << endl;

		size_t cnt;
		size_t k;

		if (bt->flags & 4)
		{
			const APFS_BTEntryFixed *e = reinterpret_cast<const APFS_BTEntryFixed *>(m_block + 0x38);

			cnt = bt->table_space_length / 4;

			for (k = 0; k < bt->key_count; k++)
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

			cnt = bt->table_space_length / 8;

			for (k = 0; k < bt->key_count; k++)
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
	const APFS_BTFooter * const tail = reinterpret_cast<const APFS_BTFooter *>(m_block + (m_blocksize - sizeof(APFS_BTFooter)));

	m_os << endl;
	m_os << "Unk-FD8  | Nodesize | Key Size | Val Size | Key Max  | Val Max  | Key Count        | Node Count " << endl;
	m_os << "---------+----------+----------+----------+----------+----------+------------------+-----------------" << endl;
	m_os << setw(8) << tail->unk_FD8 << " | ";
	m_os << setw(8) << tail->nodesize << " | ";
	m_os << setw(8) << tail->key_size << " | ";
	m_os << setw(8) << tail->val_size << " | ";
	m_os << setw(8) << tail->key_max_size << " | ";
	m_os << setw(8) << tail->val_max_size << " | ";
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

void BlockDumper::DumpBTEntry_APFS_Root(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index)
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
	// 1 = SnapMetadata
	// 2 = PhysExtent
	case 0x3:
		assert(key_length == 8);
		m_os << "3/Inode " << key << " => ";

		if (index)
		{
			assert(value_length == 8);
			m_os << hex << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			assert(value_length >= sizeof(APFS_Inode_Val));

			// m_os << "[" << setw(4) << value_length << "] ";

			if (value_length >= sizeof(APFS_Inode_Val))
			{
				const APFS_Inode_Val *obj = reinterpret_cast<const APFS_Inode_Val *>(value_data);

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
				m_os << obj->internal_flags << " ";
				m_os << obj->nchildren << " ";
				m_os << obj->default_protection_class << " ";
				m_os << obj->gen_count << " ";
				m_os << obj->bsd_flags << " ";
				m_os << obj->uid << " ";
				m_os << obj->gid << " ";
				m_os << oct << obj->mode << hex << " ";
				m_os << obj->pad1 << " ";
				m_os << obj->uncompressed_size;

				if (value_length > sizeof(APFS_Inode_Val))
					Dump_XF(value_data + sizeof(APFS_Inode_Val), value_length - sizeof(APFS_Inode_Val));

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
		m_os << "4/Xattr " << key << " ";
		// nlen = *reinterpret_cast<const uint16_t *>(key_data + 8);
		m_os << '\'' << (key_data + 10) << '\'';

		if (index)
		{
			assert(value_length == 8);
			m_os << " => " << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			const APFS_Xattr_Val *attr = reinterpret_cast<const APFS_Xattr_Val *>(value_data);

			assert(attr->size + 4 == value_length);

			m_os << " => " << attr->type << " " << attr->size;

			if ((attr->type & 1) && attr->size == 0x30)
			{
				const APFS_Xattr_External *attrlnk = reinterpret_cast<const APFS_Xattr_External *>(value_data + 4);

				m_os << " : " << attrlnk->obj_id << " " << attrlnk->stream.size << " " << attrlnk->stream.alloced_size << " ";
				m_os << attrlnk->stream.default_crypto_id << " " << attrlnk->stream.unk_18 << " " << attrlnk->stream.unk_20 << endl;
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
					// Limit length ...
					std::string str(reinterpret_cast<const char *>(value_data + 4), value_length - 4);
					m_os << " : '" << str << '\'' << endl;
				}
				else if (!strcmp(attr_name, "com.apple.decmpfs"))
				{
					if (value_length >= 20)
					{
						const CompressionHeader *cmpf = reinterpret_cast<const CompressionHeader *>(value_data + 4);
						if (cmpf->signature == 0x636D7066)
							m_os << " : 'cmpf' " << cmpf->algo << ' ' << cmpf->size;
						else
							m_os << " : [Compression Header Invalid]";
#ifdef DUMP_COMPRESSED
						if (value_length > 20)
						{
							m_os << endl;
							DumpHex(value_data + 20, value_length - 20);
						}
#else
						if (value_length > 20)
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
					DumpHex(value_data + 4, value_length - 4);
				}
			}
		}

		break;

	case 0x5:
		assert(key_length == 0x10);
		m_os << "5/SiblingLink " << key << " ";
		m_os << *reinterpret_cast<const uint64_t *>(key_data + 8) << " => ";

		if (index)
		{
			assert(value_length == 8);
			m_os << setw(16) << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		}
		else
		{
			const APFS_Sibling_Val * const v = reinterpret_cast<const APFS_Sibling_Val *>(value_data);

			m_os << v->parent_id << " ";
			m_os << "'" << v->name << "'";
			m_os << endl;
		}
		break;

	case 0x6:
		assert(key_length == 8);
		m_os << "6/DstreamID " << key << " => ";
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

	case 0x7:
	{
		assert(key_length == 8);
		const APFS_Crypto_Val *c = reinterpret_cast<const APFS_Crypto_Val *>(value_data);

		m_os << "7/CryptoState " << key << " => ";
		m_os << c->refcnt << " : ";
		m_os << c->state.major_version << " ";
		m_os << c->state.minor_version << " ";
		m_os << c->state.cpflags << " ";
		m_os << c->state.persistent_class << " ";
		m_os << c->state.key_os_version << " ";
		m_os << c->state.key_revision << " ";
		m_os << c->state.key_len << endl;

		if (value_length > sizeof(APFS_Crypto_Val))
			DumpHex(value_data + sizeof(APFS_Crypto_Val), value_length - sizeof(APFS_Crypto_Val));
	}
		break;

	case 0x8:
		assert(key_length == 16);
		m_os << "8/FileExtent " << key << " " << *reinterpret_cast<const uint64_t *>(key_data + 8) << " => ";

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
				const APFS_FileExtent_Val *ext = reinterpret_cast<const APFS_FileExtent_Val *>(value_data);
				uint16_t flags = ext->flags_length >> 56;
				uint64_t length = ext->flags_length & 0xFFFFFFFFFFFFFFULL;
				if (flags != 0)
					m_os << flags << "/";
				m_os << length << " " << ext->phys_block_num << " " << ext->crypto_id << endl;
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

		m_os << "9/DirRecord " << key << " ";

		/*
		nlen = *reinterpret_cast<const uint16_t *>(key_data + 8);
		for (size_t k = 0; k < nlen; k++)
		m_os << data[10 + k];
		*/

		if (m_text_flags & 0x09)
		{
			uint32_t hash_stored = *reinterpret_cast<const uint32_t *>(key_data + 8);

			m_os << setw(8) << hash_stored << " ";

#if 0 // Hash verify
			uint32_t hash_calc = HashFilename(reinterpret_cast<const char *>(key_data + 12), hash_stored & 0x3FF, (m_text_flags & APFS_APSB_CaseInsensitive) != 0);
			m_os << "[" << setw(8) << hash_calc << "] ";

			if (hash_calc != hash_stored)
			{
				cerr << hex << "Hash not matching at name " << key << " : stored " << hash_stored << ", calc " << hash_calc << " '";
				cerr << (key_data + 12) << "'" << endl;

				m_os << endl;
				DumpHex(key_data, key_length, key_length);
			}
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
			const APFS_DRec_Val *ptr = reinterpret_cast<const APFS_DRec_Val *>(value_data);
			m_os << ptr->file_id << " [" << tstamp(ptr->date_added) << "] " << ptr->flags;

			if (value_length > sizeof(APFS_DRec_Val))
				Dump_XF(value_data + sizeof(APFS_DRec_Val), value_length - sizeof(APFS_DRec_Val));

			m_os << endl;
		}

#if 0
		DumpHex(key_data + 12, key_length - 12);
#endif

		break;
	// TODO: A = DirSize
	// B = SnapName

	case 0xC:
		m_os << "C/SiblingMap " << key << " => " << *reinterpret_cast<const uint64_t *>(value_data) << endl;
		break;

	default:
		m_os << "KEY TYPE UNKNOWN" << endl;

		m_os << hex;
		DumpHex(key_data, key_length);
		DumpHex(value_data, value_length);
		break;
	}

#if 0
	DumpHex(key_data, key_length);
	DumpHex(value_data, value_length);
#endif

	m_os << hex;
}

void BlockDumper::DumpBTEntry_OMap(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;

	const APFS_OMap_Key *ekey = reinterpret_cast<const APFS_OMap_Key *>(key_ptr);

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
			const APFS_OMap_Val *val = reinterpret_cast<const APFS_OMap_Val *>(val_ptr);

			m_os << val->ov_flags << " ";
			m_os << val->ov_size << " ";
			m_os << val->ov_paddr << endl;
		}
	}
}

void BlockDumper::DumpBTEntry_APFS_ExtentRef(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;

	assert(key_length == 8);
	uint64_t key = *reinterpret_cast<const uint64_t *>(key_ptr);
	uint16_t type = (key >> 60) & 0xF;

	key &= 0xFFFFFFFFFFFFFFFULL;

	m_os << hex << type << "/" << key << " => ";

	if (index)
	{
		assert(value_length == 8);
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
			assert(value_length == sizeof(APFS_ExtentRef_Val));
			const APFS_ExtentRef_Val *val = reinterpret_cast<const APFS_ExtentRef_Val *>(val_ptr);
			uint64_t len;
			uint16_t flags;

			flags = (val->len >> 56) & 0xFF;
			len = val->len & 0xFFFFFFFFFFFFFFULL;

			m_os << flags << "/" << len << " ";
			m_os << val->owning_obj_id << " ";
			m_os << val->refcnt << endl;
		}
	}
}

void BlockDumper::DumpBTEntry_APFS_SnapMeta(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
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
				const APFS_SnapMetadata_Val *val = reinterpret_cast<const APFS_SnapMetadata_Val *>(val_ptr);
				m_os << setw(16) << val->extentref_tree_oid << " ";
				m_os << setw(16) << val->sblock_oid << " ";
				// m_os << setw(16) << val->change_time << " ";
				// m_os << setw(16) << val->create_time << " ";
				m_os << "[" << tstamp(val->change_time) << "] ";
				m_os << "[" << tstamp(val->create_time) << "] ";
				m_os << setw(16) << val->inum << " ";
				m_os << setw(8) << val->extentref_tree_type << " ";
				m_os << setw(8) << val->flags << " ";
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

void BlockDumper::DumpBTEntry_13(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;
	(void)index;

	assert(key_length == 8);

	m_os << *reinterpret_cast<const uint64_t *>(key_ptr) << " => ";

	if (index)
	{
		assert(value_length == 0x08);

		m_os << *reinterpret_cast<const uint64_t *>(val_ptr);
	}
	else
	{
		assert(value_length == 0x10);

		m_os << *reinterpret_cast<const uint64_t *>(val_ptr) << " ";
		m_os << *reinterpret_cast<const uint64_t *>(val_ptr + 8);
	}

	// DumpHex(key_ptr, 0x08);
	// DumpHex(val_ptr, 0x10);
	m_os << endl;
}

void BlockDumper::DumpBTEntry_FreeList(const byte_t * key_ptr, size_t key_length, const byte_t * val_ptr, size_t value_length, bool index)
{
	(void)key_length;
	(void)value_length;
	(void)index;

	const APFS_Key_8_9 *key = reinterpret_cast<const APFS_Key_8_9 *>(key_ptr);

	assert(key_length == 0x10);
	assert(value_length == 0x08);

	m_os << key->xid << " ";
	m_os << key->bid << " => ";

	if (!val_ptr)
		m_os << "1/NULL" << endl;
	else
		m_os << *reinterpret_cast<const uint64_t *>(val_ptr) << endl;
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

void BlockDumper::Dump_XF(const byte_t * xf_data, size_t xf_size)
{
	const APFS_XF_Header *h = reinterpret_cast<const APFS_XF_Header *>(xf_data);
	const APFS_XF_Entry *e = reinterpret_cast<const APFS_XF_Entry *>(xf_data + sizeof(APFS_XF_Header));
	uint16_t entry_base = h->xf_num_exts * sizeof(APFS_XF_Entry) + sizeof(APFS_XF_Header);
	uint16_t k;
	const byte_t *data;

	if (xf_size < 4)
	{
		m_os << " [!!!XF size too small!!!]" << endl;
		return;
	}

	m_os << " XF: " << h->xf_num_exts << " " << h->xf_used_data << " : ";

	for (k = 0; k < h->xf_num_exts; k++)
	{
		m_os << setw(2) << static_cast<int>(e[k].xf_type) << " ";
		m_os << setw(2) << static_cast<int>(e[k].xf_flags) << " ";
		m_os << setw(4) << e[k].xf_length << " : ";
	}

	for (k = 0; k < h->xf_num_exts; k++)
	{
		data = xf_data + entry_base;

		switch (e[k].xf_type)
		{
		case DREC_EXT_TYPE_SIBLING_ID:
			m_os << "[SIB_ID] " << *reinterpret_cast<const uint64_t *>(data);
			break;

		case INO_EXT_TYPE_DOCUMENT_ID:
			m_os << "[DOC_ID] " << *reinterpret_cast<const uint32_t *>(data);
			break;

		case INO_EXT_TYPE_NAME:
			m_os << "[NAME] '" << data << "'";
			break;

		case INO_EXT_TYPE_DSTREAM:
		{
			const APFS_DStream *ft = reinterpret_cast<const APFS_DStream *>(data);
			m_os << "[DSTREAM] ";
			m_os << ft->size << " ";
			m_os << ft->alloced_size << " ";
			m_os << ft->default_crypto_id << " ";
			m_os << ft->unk_18 << " ";
			m_os << ft->unk_20;
			break;
		}

		case INO_EXT_TYPE_DIR_STATS_KEY:
			m_os << "[DIR_STATS] " << *reinterpret_cast<const uint64_t *>(data);
			break;

		case INO_EXT_TYPE_FS_UUID:
			m_os << "[FS_UUID] " << uuidstr(*reinterpret_cast<const apfs_uuid_t *>(data));
			break;

		case INO_EXT_TYPE_SPARSE_BYTES:
			m_os << "[SPARSE] " << *reinterpret_cast<const uint64_t *>(data);
			break;

		default:
			m_os << "[!!!UNKNOWN!!!] ";
			DumpHex(data, e[k].xf_length, e[k].xf_length);
		}

		entry_base += ((e[k].xf_length + 7) & 0xFFF8);

		if (k < (h->xf_num_exts - 1))
			m_os << " : ";
	}
}

void BlockDumper::DumpBlk_APSB()
{
	const APFS_Superblock_APSB * const sb = reinterpret_cast<const APFS_Superblock_APSB *>(m_block);
	int k;

	m_os << hex;
	m_os << "magic            : " << setw(8) << sb->apfs_magic << endl;
	m_os << "fs_index         : " << setw(8) << sb->apfs_fs_index << endl;
	m_os << "features         : " << setw(16) << sb->apfs_features << endl;
	m_os << "ro_compat_feat   : " << setw(16) << sb->apfs_readonly_compatible_features << endl;
	m_os << "incompat_feat    : " << setw(16) << sb->apfs_incompatible_features << endl;
	m_os << "unmount_time     : " << setw(16) << sb->apfs_unmount_time << endl;
	m_os << "reserve_blk_cnt  : " << setw(16) << sb->apfs_reserve_block_count << endl;
	m_os << "quota_blk_cnt    : " << setw(16) << sb->apfs_quota_block_count << endl;
	m_os << "fs_alloc_cnt     : " << setw(16) << sb->apfs_fs_alloc_count << endl;
	m_os << "Unknown 0x60     : " << setw(16) << sb->unk_60 << endl;
	m_os << "Unknown 0x68     : " << setw(8) << sb->unk_68 << endl;
	m_os << "Unknown 0x6C     : " << setw(8) << sb->unk_6C << endl;
	m_os << "Unknown 0x70     : " << setw(8) << sb->unk_70 << endl;
	m_os << "root_tree_type   : " << setw(8) << sb->apfs_root_tree_type << endl;
	m_os << "extentref_tree_t : " << setw(8) << sb->apfs_extentref_tree_type << endl;
	m_os << "snap_meta_tree_t : " << setw(8) << sb->apfs_snap_meta_tree_type << endl;
	m_os << "omap_oid         : " << setw(16) << sb->apfs_omap_oid << endl;
	m_os << "root_tree_oid    : " << setw(16) << sb->apfs_root_tree_oid << endl;
	m_os << "extentref_tree_o : " << setw(16) << sb->apfs_extentref_tree_oid << endl;
	m_os << "snap_meta_tree_o : " << setw(16) << sb->apfs_snap_meta_tree_oid << endl;
	m_os << "revert_to_xid    : " << setw(16) << sb->apfs_revert_to_xid << endl;
	m_os << "Unknown 0xA8     : " << setw(16) << sb->unk_A8 << endl;
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
	m_os << "fs_flags         : " << setw(16) << sb->apfs_fs_flags << endl;
	m_os << "formatted_by id  : " << sb->apfs_formatted_by.id << endl;
	m_os << "    timestamp    : " << tstamp(sb->apfs_formatted_by.timestamp) << endl;
	m_os << "    last_xid     : " << sb->apfs_formatted_by.last_xid << endl;
	for (k = 0; k < 8; k++)
	{
		m_os << "modified_by id   : " << sb->apfs_modified_by[k].id << endl;
		m_os << "    timestamp    : " << tstamp(sb->apfs_modified_by[k].timestamp) << endl;
		m_os << "    last_xid     : " << setw(16) << sb->apfs_modified_by[k].last_xid << endl;
	}
	m_os << "volname          : " << sb->apfs_volname << endl;
	m_os << "next_doc_id      : " << setw(8) << sb->apfs_next_doc_id << endl;
	m_os << "Unknown 0x3C4    : " << setw(8) << sb->unk_3C4 << endl;
	m_os << "Unknown 0x3C8    : " << setw(16) << sb->unk_3C8 << endl;

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_CAB()
{
	const APFS_ChunkABlock * const cab = reinterpret_cast<const APFS_ChunkABlock *>(m_block);

	size_t k;
	size_t cnt;

	dumpm("id     ", cab, cab->flags);
	dumpm("count  ", cab, cab->count);

	m_os << endl;

	cnt = cab->count;

	for (k = 0; k < cnt; k++)
	{
		dumpm("cib oid", cab, cab->entry[k]);
	}
}

void BlockDumper::DumpBlk_CIB()
{
	const APFS_ChunkInfoBlock * const cib = reinterpret_cast<const APFS_ChunkInfoBlock *>(m_block);

	size_t k;

	dumpm("id         ", cib, cib->id);
	dumpm("chunk_count", cib, cib->chunk_count);
	m_os << endl;

	m_os << "Xid              | Offset           | Bits Tot | Bits Avl | Block" << endl;
	m_os << "-----------------+------------------+----------+----------+-----------------" << endl;

	for (k = 0; k < cib->chunk_count; k++)
	{
		m_os << setw(16) << cib->chunk[k].xid << " | ";
		m_os << setw(16) << cib->chunk[k].offset << " | ";
		m_os << setw(8) << cib->chunk[k].bits_total << " | ";
		m_os << setw(8) << cib->chunk[k].bits_avail << " | ";
		m_os << setw(16) << cib->chunk[k].block << endl;
	}
}

void BlockDumper::DumpBlk_OM()
{
	const APFS_OMap_Root * const om = reinterpret_cast<const APFS_OMap_Root *>(m_block);

	dumpm("?              ", om, om->unk_20);
	dumpm("?              ", om, om->unk_24);

	// DumpTableHeader(blk->tbl);

	dumpm("om_type_1      ", om, om->type_1);
	dumpm("om_type_2      ", om, om->type_2);
	dumpm("om_oid         ", om, om->om_tree_oid);
	dumpm("oms_oid        ", om, om->oms_tree_oid);
	dumpm("oms_xid        ", om, om->oms_tree_xid);
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

	if (!IsZero(m_block + 0x38, 0xFC8))
	{
		m_os << "!!! ADDITIONAL DATA !!!" << endl;
		DumpBlockHex();
	}
}

void BlockDumper::DumpBlk_CPM()
{
	const APFS_CheckPointMap * const cpm = reinterpret_cast<const APFS_CheckPointMap *>(m_block);

	size_t k;

	dumpm("cpm_flags", cpm, cpm->cpm_flags);
	dumpm("cpm_count", cpm, cpm->cpm_count);
	m_os << endl;

	m_os << "Type     | Subtype  | Size     | Unk-0C   | FS-OID?          | OID              | PAddr" << endl;
	m_os << "---------+----------+----------+----------+------------------+------------------+-----------------" << endl;

	for (k = 0; k < cpm->cpm_count; k++)
	{
		m_os << setw(8) << cpm->cpm_map[k].cpm_type << " | ";
		m_os << setw(8) << cpm->cpm_map[k].cpm_subtype << " | ";
		m_os << setw(8) << cpm->cpm_map[k].cpm_size << " | ";
		m_os << setw(8) << cpm->cpm_map[k].cpm_unk_C << " | ";
		m_os << setw(16) << cpm->cpm_map[k].cpm_fs_oid << " | ";
		m_os << setw(16) << cpm->cpm_map[k].cpm_oid << " | ";
		m_os << setw(16) << cpm->cpm_map[k].cpm_paddr << endl;
	}
}

void BlockDumper::DumpBlk_NXSB()
{
	const APFS_NX_Superblock * const nx = reinterpret_cast<const APFS_NX_Superblock *>(m_block);
	size_t k;

	m_os << hex;
	dumpm("magic           ", nx, nx->nx_magic);
	dumpm("block_size      ", nx, nx->nx_block_size);
	dumpm("block_count     ", nx, nx->nx_block_count);
	dumpm("features        ", nx, nx->nx_features);
	dumpm("ro_compat_feat's", nx, nx->nx_read_only_compatible_features);
	dumpm("incompat_feat's ", nx, nx->nx_incompatible_features);
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
	dumpm("?               ", nx, nx->unk_B0);
	dumpm("max_file_systems", nx, nx->nx_max_file_systems);
	m_os << endl;

	for (k = 0; k < nx->nx_max_file_systems && nx->nx_fs_oid[k] != 0; k++)
		dumpm("fs_oid          ", nx, nx->nx_fs_oid[k]);

	/*
	m_os << "magic            : " << setw(8) << nx->nx_magic << endl;
	m_os << "block_size       : " << setw(8) << nx->nx_block_size << endl;
	m_os << "block_count      : " << setw(16) << nx->nx_block_count << endl;
	m_os << "features         : " << setw(16) << nx->nx_features << endl;
	m_os << "ro_compat_feat's : " << setw(16) << nx->nx_read_only_compatible_features << endl;
	m_os << "incompat_feat's  : " << setw(16) << nx->nx_incompatible_features << endl;
	m_os << "uuid             : " << setw(16) << uuidstr(nx->nx_uuid) << endl;
	m_os << "next_oid         : " << setw(16) << nx->nx_next_oid << endl;
	m_os << "next_xid         : " << setw(16) << nx->nx_next_xid << endl;
	m_os << "xp_desc_blocks   : " << setw(8) << nx->nx_xp_desc_blocks << endl;
	m_os << "xp_data_blocks   : " << setw(8) << nx->nx_xp_data_blocks << endl;
	m_os << "xp_desc_base     : " << setw(16) << nx->nx_xp_desc_base << endl;
	m_os << "xp_data_base     : " << setw(16) << nx->nx_xp_data_base << endl;
	m_os << "xp_desc_next     : " << setw(8) << nx->nx_xp_desc_next << endl;
	m_os << "xp_data_next     : " << setw(8) << nx->nx_xp_data_next << endl;
	m_os << "xp_desc_index    : " << setw(8) << nx->nx_xp_desc_index << endl;
	m_os << "xp_desc_len      : " << setw(8) << nx->nx_xp_desc_len << endl;
	m_os << "xp_data_index    : " << setw(8) << nx->nx_xp_data_index << endl;
	m_os << "xp_data_len      : " << setw(8) << nx->nx_xp_data_len << endl;
	m_os << "spaceman_oid     : " << setw(16) << nx->nx_spaceman_oid << endl;
	m_os << "omap_oid         : " << setw(16) << nx->nx_omap_oid << endl;
	m_os << "reaper_oid       : " << setw(16) << nx->nx_reaper_oid << endl;
	m_os << "Unknown 0xB0     : " << setw(8) << nx->unk_B0 << endl;
	m_os << "max_file_systems : " << setw(8) << nx->nx_max_file_systems << endl;

	for (size_t k = 0; (k < 100) && (nx->nx_fs_oid[k] != 0); k++)
		m_os << "fs_oid       " << setw(2) << k << "  : " << setw(16) << nx->nx_fs_oid[k] << endl;
	*/
	m_os << endl;

	/*
	m_os << "keybag_base      : " << setw(16) << nx->nx_keybag_base << endl;
	m_os << "keybag_blocks    : " << setw(16) << nx->nx_keybag_blocks << endl;
	*/

	dumpm("blocked_out_base", nx, nx->nx_blocked_out_base);
	dumpm("blocked_out_blks", nx, nx->nx_blocked_out_blocks);
	dumpm("                ", nx, nx->unk_4E8);
	dumpm("                ", nx, nx->unk_4F0);
	dumpm("efi_js_paddr    ", nx, nx->nx_efi_jumpstart_paddr);
	dumpm("                ", nx, nx->unk_500);
	dumpm("                ", nx, nx->unk_508);
	dumpm("keybag_base     ", nx, nx->nx_keybag_base);
	dumpm("keybag_blocks   ", nx, nx->nx_keybag_blocks);


	m_os << endl;

	DumpBlockHex();
}

void BlockDumper::DumpBlk_SM()
{
	size_t k;
	size_t cnt;
	const APFS_Spaceman *b = reinterpret_cast<const APFS_Spaceman *>(m_block);

	m_os << hex;
	dumpm("block_size          ", b, b->block_size);
	dumpm("blocks_per_chunk    ", b, b->blocks_per_chunk);
	dumpm("chunks_per_cib      ", b, b->chunks_per_cib);
	dumpm("cibs_per_cab        ", b, b->cibs_per_cab);
	dumpm("block_count         ", b, b->block_count);
	dumpm("chunk_count         ", b, b->chunk_count);
	dumpm("cib_count           ", b, b->cib_count);
	dumpm("cab_count           ", b, b->cab_count);
	dumpm("free_count          ", b, b->free_count);
	dumpm("cib_arr_offs?       ", b, b->cib_arr_offs);
	dumpm("?                   ", b, b->unk_58);
	dumpm("t2_block_count      ", b, b->tier2_block_count);
	dumpm("t2_chunk_count      ", b, b->tier2_chunk_count);
	dumpm("t2_cib_count        ", b, b->tier2_cib_count);
	dumpm("t2_cab_count        ", b, b->tier2_cab_count);
	dumpm("t2_free_count       ", b, b->tier2_free_count);
	dumpm("t2_cib_arr_offs?    ", b, b->tier2_cib_arr_offs);
	dumpm("?                   ", b, b->unk_88);
	dumpm("?                   ", b, b->unk_90);
	dumpm("?                   ", b, b->unk_94);
	dumpm("ip_block_count      ", b, b->ip_block_count);
	dumpm("ip_bm_block_cnt     ", b, b->ip_bm_block_count);
	dumpm("ip_bitmap_blk_cnt   ", b, b->ip_bitmap_block_count);
	dumpm("ip_bm_base          ", b, b->ip_bm_base_address);
	dumpm("ip_base             ", b, b->ip_base_address);
	dumpm("?                   ", b, b->unk_B8);
	dumpm("?                   ", b, b->unk_C0);
	dumpm("fq_count_1          ", b, b->free_queue_count_1);
	dumpm("fq_tree_1           ", b, b->free_queue_tree_1);
	dumpm("?                   ", b, b->unk_D8);
	dumpm("?                   ", b, b->unk_E0);
	dumpm("?                   ", b, b->unk_E8);
	dumpm("fq_count_2          ", b, b->free_queue_count_2);
	dumpm("fq_tree_2           ", b, b->free_queue_tree_2);
	dumpm("?                   ", b, b->unk_100);
	dumpm("?                   ", b, b->unk_108);
	dumpm("?                   ", b, b->unk_110);
	dumpm("fq_count_3          ", b, b->free_queue_count_3);
	dumpm("fq_tree_3           ", b, b->free_queue_tree_3);
	dumpm("?                   ", b, b->unk_128);
	dumpm("?                   ", b, b->unk_130);
	dumpm("?                   ", b, b->unk_138);
	dumpm("bmp_next_arr_free   ", b, b->bitmap_next_array_free);
	dumpm("?                   ", b, b->unk_142);
	dumpm("?                   ", b, b->unk_144);
	dumpm("?                   ", b, b->unk_148);
	dumpm("array offset ?      ", b, b->unk_array_offs);
	dumpm("?                   ", b, b->unk_150);
	dumpm("?                   ", b, b->unk_154);
	dumpm("?                   ", b, b->unk_158);
	m_os << endl;

	/*
	for (k = 0; k < 0x10; k++)
		dumpm("?                   ", b, b->unk_160[k]);
	m_os << endl;
	*/

	const le<uint16_t> *unk_arr = reinterpret_cast<const le<uint16_t> *>(m_block + b->unk_array_offs);
	for (k = 0; k < b->ip_bitmap_block_count; k++)
		dumpm("?                   ", b, unk_arr[k]);

	if (b->cab_count > 0)
		cnt = b->cab_count;
	else
		cnt = b->cib_count;

	if (b->cib_arr_offs != 0 && cnt != 0)
	{
		const le<uint64_t> *cib_oid = reinterpret_cast<const le<uint64_t> *>(m_block + b->cib_arr_offs);

		for (k = 0; k < cnt; k++)
			dumpm("oid                 ", b, cib_oid[k]);
	}

	m_os << endl;

	if (b->unk_144 != 0)
	{
		const le<uint64_t> &unk_xid = *reinterpret_cast<const le<uint64_t> *>(m_block + b->unk_144);
		dumpm("unk_144->?          ", b, unk_xid);
	}

	if (b->unk_148 != 0)
	{
		const le<uint64_t> &unk_148 = *reinterpret_cast<const le<uint64_t> *>(m_block + b->unk_148);
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

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_NR()
{
	const APFS_NX_Reaper *nr = reinterpret_cast<const APFS_NX_Reaper *>(m_block);

	m_os << hex;
	m_os << "unk 20 : " << setw(16) << nr->unk_20 << endl;
	m_os << "unk 28 : " << setw(16) << nr->unk_28 << endl;
	m_os << "unk 30 : " << setw(16) << nr->unk_30 << endl;
	m_os << "unk 38 : " << setw(16) << nr->unk_38 << endl;
	m_os << "unk 40 : " << setw(8) << nr->unk_40 << endl;
	m_os << "unk 44 : " << setw(8) << nr->unk_44 << endl;
	m_os << "unk 48 : " << setw(8) << nr->unk_48 << endl;
	m_os << "unk 4C : " << setw(8) << nr->unk_4C << endl;
	m_os << "unk 50 : " << setw(16) << nr->unk_50 << endl;
	m_os << "unk 58 : " << setw(16) << nr->unk_58 << endl;
	m_os << "unk 60 : " << setw(16) << nr->unk_60 << endl;
	m_os << "unk 68 : " << setw(8) << nr->unk_68 << endl;
	m_os << "unk 6C : " << setw(8) << nr->unk_6C << endl;

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_NRL()
{
	const APFS_NX_ReapList *nrl = reinterpret_cast<const APFS_NX_ReapList *>(m_block);
	uint32_t k;

	m_os << hex;
	m_os << "unk 20      : " << setw(8) << nrl->unk_20 << endl;
	m_os << "unk 24      : " << setw(8) << nrl->unk_24 << endl;
	m_os << "unk 28      : " << setw(8) << nrl->unk_28 << endl;
	m_os << "max rec cnt : " << setw(8) << nrl->max_record_count << endl;
	m_os << "record cnt  : " << setw(8) << nrl->record_count << endl;
	m_os << "first idx   : " << setw(8) << nrl->first_index << endl;
	m_os << "last idx    : " << setw(8) << nrl->last_index << endl;
	m_os << "free idx    : " << setw(8) << nrl->free_index << endl;
	m_os << endl;
	m_os << "fwlink   | unk      | type     | blksize  | oid              | paddr            | xid" << endl;
	m_os << "---------+----------+----------+----------+------------------+------------------+-----------------" << endl;
	for (k = 0; k < nrl->max_record_count; k++)
	{
		m_os << setw(8) << nrl->nrle[k].fwlink << " | ";
		m_os << setw(8) << nrl->nrle[k].unk_04 << " | ";
		m_os << setw(8) << nrl->nrle[k].type << " | ";
		m_os << setw(8) << nrl->nrle[k].blksize << " | ";
		m_os << setw(16) << nrl->nrle[k].oid << " | ";
		m_os << setw(16) << nrl->nrle[k].paddr << " | ";
		m_os << setw(16) << nrl->nrle[k].xid << endl;
	}

	m_os << endl;
	DumpBlockHex();
}

void BlockDumper::DumpBlk_JSDR()
{
	const APFS_EFI_JumpStart *js = reinterpret_cast<const APFS_EFI_JumpStart *>(m_block);

	dumpm("magic           ", js, js->magic);
	dumpm("?               ", js, js->unk_24);
	dumpm("filesize        ", js, js->filesize);
	dumpm("extent count    ", js, js->extent_count);
	m_os << "..." << endl;

	for (size_t k = 0; k < js->extent_count; k++)
	{
		dumpm("apfs.efi base   ", js, js->extent[k].base);
		dumpm("apfs.efi blocks ", js, js->extent[k].blocks);
	}

	m_os << endl;

	DumpBlockHex();
}

void BlockDumper::DumpBTNode_0()
{
	const APFS_ObjHeader * const hdr = reinterpret_cast<const APFS_ObjHeader *>(m_block);

	switch (hdr->subtype)
	{
	case 0x09:
		DumpBTNode(&BlockDumper::DumpBTEntry_FreeList, 0x10, 0x08);
		break;

	case 0x0B:
		DumpBTNode(&BlockDumper::DumpBTEntry_OMap, 0x10, 0x10);
		break;

	case 0x0E:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_Root);
		break;

	case 0x0F:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_ExtentRef);
		break;

	case 0x10:
		DumpBTNode(&BlockDumper::DumpBTEntry_APFS_SnapMeta);
		break;

	case 0x13:
		DumpBTNode(&BlockDumper::DumpBTEntry_13, 0x8, 0x10);
		break;

	case 0x1A:
		DumpBTNode(&BlockDumper::DumpBTEntry_Unk, 8, 8);
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

	switch (type & 0xFFFFFFF)
	{
	case 0x0001:
		typestr = "nx / Container Superblock (NXSB)";
		break;
	case 0x0002:
	case 0x0003:
		switch (subtype)
		{
		case 0x00000009:
			typestr = "btn / fq / Free Queue Tree";
			break;
		case 0x0000000B:
			typestr = "btn / om / O-Map Tree";
			break;
		case 0x0000000E:
			typestr = "btn / apfs_root / Directory Tree";
			break;
		case 0x0000000F:
			typestr = "btn / apfs_extentref / Extent-Ref Tree";
			break;
		case 0x00000010:
			typestr = "btn / apfs_snap_meta / Snapshot Metadata Tree";
			break;
		case 0x00000013:
			typestr = "btn / oms / (?)";
			break;
		default:
			typestr = "btn / (?)";
			break;
		}
		break;
	case 0x4:
		typestr = "mt / MTree (?)";
		break;
	case 0x0005:
		typestr = "sm / Spaceman Header";
		break;
	case 0x6:
		typestr = "cab / Chunk Alloc? Block (?)";
		break;
	case 0x0007:
		typestr = "cib / Chunk Info Block";
		break;
	case 0x0008:
		typestr = "sm_ip / Spaceman IP (?)";
		break;
	case 0x000B:
		typestr = "om / OMap";
		break;
	case 0x000C:
		typestr = "cpm / Checkpoint Map (?)";
		break;
	case 0x000D:
		typestr = "apfs / Volume Superblock (APSB)";
		break;
	case 0x0011:
		typestr = "nr / NX Reaper";
		break;
	case 0x12:
		typestr = "nrl / NX Reap List";
		break;
	case 0x14:
		typestr = "jsdr / EFI JumpStart Record";
		break;
	case 0x16:
		typestr = "wbc / (?)";
		break;
	case 0x17:
		typestr = "wbcl / (?)";
		break;
	}

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

void BlockDumper::dumpm(const char* name, const void *base, const uint8_t& v)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u8  ";
#endif

	m_os << name << " : " << setw(2) << static_cast<unsigned>(v) << endl;
}

void BlockDumper::dumpm(const char* name, const void *base, const le<uint16_t>& v)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u16 ";
#endif

	m_os << name << " : " << setw(4) << v.get() << endl;
}

void BlockDumper::dumpm(const char* name, const void *base, const le<uint32_t>& v)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u32 ";
#endif

	m_os << name << " : " << setw(8) << v.get() << endl;
}

void BlockDumper::dumpm(const char* name, const void *base, const le<uint64_t>& v)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&v) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " u64 ";
#endif

	m_os << name << " : " << setw(16) << v.get() << endl;
}

void BlockDumper::dumpm(const char* name, const void* base, const apfs_uuid_t& uuid)
{
#ifdef DUMP_OT
	ptrdiff_t d = reinterpret_cast<uintptr_t>(&uuid) - reinterpret_cast<uintptr_t>(base);
	m_os << setw(4) << d << " uid ";
#endif

	m_os << name << " : " << setw(16) << uuidstr(uuid) << endl;
}

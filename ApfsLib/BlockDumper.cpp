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

	switch (node->o_type & 0xFFFFFFF)
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

	default:
		// assert(false);
		std::cerr << "!!! UNKNOWN NODE TYPE " << hex << setw(8) << node->o_type << " in block " << setw(16) << blk_nr << " !!!" << endl;
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
	m_os << setw(16) << blk->o_cksum << " | ";
	m_os << setw(16) << blk->o_oid << " | ";
	m_os << setw(16) << blk->o_xid << " | ";
	m_os << setw(8) << blk->o_type << " | ";
	m_os << setw(8) << blk->o_subtype << " | ";
	m_os << GetNodeType(blk->o_type, blk->o_subtype) << endl;
	m_os << endl;
}


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

	DumpBTHeader();

	base = bt->table_space_offset + bt->table_space_length + 0x38;
	end = (APFS_OBJ_TYPE(hdr->o_type) == BlockType_BTRoot) ? (m_blocksize - sizeof(APFS_BTFooter)) : m_blocksize;

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

			if (APFS_OBJ_TYPE(hdr->o_type) == BlockType_BTRoot)
				DumpBTFooter();

			return;
		}

		const APFS_BTEntryFixed * const entry = reinterpret_cast<const APFS_BTEntryFixed *>(m_block + (bt->table_space_offset + 0x38));

		for (k = 0; k < bt->key_count; k++)
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
		const APFS_BTEntry * const entry = reinterpret_cast<const APFS_BTEntry *>(m_block + (bt->table_space_offset + 0x38));

		for (k = 0; k < bt->key_count; k++)
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

	if (APFS_OBJ_TYPE(hdr->o_type) == BlockType_BTRoot)
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
	const APFS_BTFooter * const tail = reinterpret_cast<const APFS_BTFooter *>(m_block + 0xFD8);

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

#if 1 // Hash verify
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
				// m_os << setw(16) << val->tstamp_10 << " ";
				// m_os << setw(16) << val->tstamp_18 << " ";
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

	DumpHex(key_ptr, 0x08);
	DumpHex(val_ptr, 0x10);
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

void BlockDumper::DumpBlk_CIB()
{
	const APFS_Block_4_7_Bitmaps * const blk = reinterpret_cast<const APFS_Block_4_7_Bitmaps *>(m_block);

	size_t k;

	DumpTableHeader(blk->tbl);

	m_os << "Xid              | Offset           | Bits Tot | Bits Avl | Block" << endl;
	m_os << "-----------------+------------------+----------+----------+-----------------" << endl;

	for (k = 0; k < blk->tbl.entries_cnt; k++)
	{
		m_os << setw(16) << blk->bmp[k].xid << " | ";
		m_os << setw(16) << blk->bmp[k].offset << " | ";
		m_os << setw(8) << blk->bmp[k].bits_total << " | ";
		m_os << setw(8) << blk->bmp[k].bits_avail << " | ";
		m_os << setw(16) << blk->bmp[k].block << endl;
	}
}

void BlockDumper::DumpBlk_OM()
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

void BlockDumper::DumpBlk_CPM()
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
		m_os << setw(16) << blk->entry[k].nid << " | ";
		m_os << setw(16) << blk->entry[k].block << endl;
	}
}

void BlockDumper::DumpBlk_NXSB()
{
	const APFS_NX_Superblock * const sb = reinterpret_cast<const APFS_NX_Superblock *>(m_block);

	m_os << hex;
	m_os << "magic            : " << setw(8) << sb->nx_magic << endl;
	m_os << "block_size       : " << setw(8) << sb->nx_block_size << endl;
	m_os << "block_count      : " << setw(16) << sb->nx_block_count << endl;
	m_os << "features         : " << setw(16) << sb->nx_features << endl;
	m_os << "ro_compat_feat's : " << setw(16) << sb->nx_read_only_compatible_features << endl;
	m_os << "incompat_feat's  : " << setw(16) << sb->nx_incompatible_features << endl;
	m_os << "uuid             : " << setw(16) << uuidstr(sb->nx_uuid) << endl;
	m_os << "next_oid         : " << setw(16) << sb->nx_next_oid << endl;
	m_os << "next_xid         : " << setw(16) << sb->nx_next_xid << endl;
	m_os << "xp_desc_blocks   : " << setw(8) << sb->nx_xp_desc_blocks << endl;
	m_os << "xp_data_blocks   : " << setw(8) << sb->nx_xp_data_blocks << endl;
	m_os << "xp_desc_base     : " << setw(16) << sb->nx_xp_desc_base << endl;
	m_os << "xp_data_base     : " << setw(16) << sb->nx_xp_data_base << endl;
	m_os << "xp_desc_next     : " << setw(8) << sb->nx_xp_desc_next << endl;
	m_os << "xp_data_next     : " << setw(8) << sb->nx_xp_data_next << endl;
	m_os << "xp_desc_index    : " << setw(8) << sb->nx_xp_desc_index << endl;
	m_os << "xp_desc_len      : " << setw(8) << sb->nx_xp_desc_len << endl;
	m_os << "xp_data_index    : " << setw(8) << sb->nx_xp_data_index << endl;
	m_os << "xp_data_len      : " << setw(8) << sb->nx_xp_data_len << endl;
	m_os << "spaceman_oid     : " << setw(16) << sb->nx_spaceman_oid << endl;
	m_os << "omap_oid         : " << setw(16) << sb->nx_omap_oid << endl;
	m_os << "reaper_oid       : " << setw(16) << sb->nx_reaper_oid << endl;
	m_os << "Unknown 0xB0     : " << setw(8) << sb->unk_B0 << endl;
	m_os << "max_file_systems : " << setw(8) << sb->nx_max_file_systems << endl;

	for (size_t k = 0; (k < 100) && (sb->nx_fs_oid[k] != 0); k++)
		m_os << "fs_oid       " << setw(2) << k << "  : " << setw(16) << sb->nx_fs_oid[k] << endl;

	m_os << endl;

	m_os << "keybag_base      : " << setw(16) << sb->nx_keybag_base << endl;
	m_os << "keybag_blocks    : " << setw(16) << sb->nx_keybag_blocks << endl;

	m_os << endl;

	DumpBlockHex();
}

void BlockDumper::DumpBlk_SM()
{
	size_t k;
	const APFS_Block_8_5_Spaceman *b = reinterpret_cast<const APFS_Block_8_5_Spaceman *>(m_block);

	m_os << hex;
	m_os << "block_size       : " << setw(8) << b->block_size << endl;
	m_os << "blocks_per_chunk : " << setw(8) << b->blocks_per_chunk << endl;
	m_os << "chunks_per_cib   : " << setw(8) << b->chunks_per_cib << endl;
	m_os << "cibs_per_cab     : " << setw(8) << b->cibs_per_cab << endl;
	m_os << "block_count      : " << setw(16) << b->block_count << endl;
	m_os << "chunk_count      : " << setw(16) << b->chunk_count << endl;
	m_os << "cib_count        : " << setw(8) << b->cib_count << endl;
	m_os << "cab_count        : " << setw(8) << b->cab_count << endl;
	m_os << "free_count       : " << setw(16) << b->free_count << endl;
	m_os << "0050             : " << setw(16) << b->cib_arr_offs << endl;
	m_os << "0058             : " << setw(16) << b->unk_58 << endl;
	m_os << "tier2_block_count: " << setw(16) << b->tier2_block_count << endl;
	m_os << "tier2_chunk_count: " << setw(16) << b->tier2_chunk_count << endl;
	m_os << "tier2_cib_count  : " << setw(8) << b->tier2_cib_count << endl;
	m_os << "tier2_cab_count  : " << setw(8) << b->tier2_cab_count << endl;
	m_os << "tier2_free_count : " << setw(16) << b->tier2_free_count << endl;
	m_os << "0080             : " << setw(16) << b->tier2_cib_arr_offs << endl;
	m_os << "0088             : " << setw(16) << b->unk_88 << endl;
	m_os << "0090             : " << setw(8) << b->unk_90 << endl;
	m_os << "0094             : " << setw(8) << b->unk_94 << endl;
	m_os << "ip_block_count   : " << setw(16) << b->ip_block_count << endl;
	m_os << "ip_bm_block_count: " << setw(8) << b->ip_bm_block_count << endl;
	m_os << "ip_bitmap_block_c: " << setw(8) << b->ip_bitmap_block_count << endl;
	m_os << "ip_bm_base_addr  : " << setw(16) << b->ip_bm_base_address << endl;
	m_os << "ip_base_addr     : " << setw(16) << b->ip_base_address << endl;
	m_os << "00B8             : " << setw(16) << b->unk_B8 << endl;
	m_os << "00C0             : " << setw(16) << b->unk_C0 << endl;
	m_os << "free_queue_count : " << setw(16) << b->free_queue_count_1 << endl;
	m_os << "free_queue_tree_1: " << setw(16) << b->free_queue_tree_1 << endl;
	m_os << "00D8             : " << setw(16) << b->unk_D8 << endl;
	m_os << "00E0             : " << setw(16) << b->unk_E0 << endl;
	m_os << "00E8             : " << setw(16) << b->unk_E8 << endl;
	m_os << "free_queue_count : " << setw(16) << b->free_queue_count_2 << endl;
	m_os << "free_queue_tree_2: " << setw(16) << b->free_queue_tree_2 << endl;
	m_os << "0100             : " << setw(16) << b->unk_100 << endl;
	m_os << "0108             : " << setw(16) << b->unk_108 << endl;
	m_os << "0110             : " << setw(16) << b->unk_110 << endl;
	m_os << "free_queue_count : " << setw(16) << b->free_queue_count_3 << endl;
	m_os << "free_queue_tree_3: " << setw(16) << b->free_queue_tree_3 << endl;
	m_os << "0128             : " << setw(16) << b->unk_128 << endl;
	m_os << "0130             : " << setw(16) << b->unk_130 << endl;
	m_os << "0138             : " << setw(16) << b->unk_138 << endl;
	m_os << "bmp_next_arr_free: " << setw(4) << b->bitmap_next_array_free << endl;
	m_os << "0142             : " << setw(4) << b->unk_142 << endl;
	m_os << "0144             : " << setw(8) << b->unk_144 << endl;
	m_os << "0148             : " << setw(8) << b->unk_148 << endl;
	m_os << "014C             : " << setw(8) << b->unk_14C << endl;
	m_os << "0150             : " << setw(16) << b->unk_150 << endl;
	m_os << "0158             : " << setw(16) << b->unk_158 << endl;
	for (k = 0; k < 0x10; k++)
		m_os << setw(4) << (0x160 + 2 * k) << "             : " << setw(4) << b->unk_160[k] << endl;
	m_os << "0180 BID BmpHdr  : " << setw(16) << b->blockid_vol_bitmap_hdr << endl;
	m_os << "..." << endl;
	m_os << "09D8 Some XID    : " << setw(16) << b->some_xid_9D8 << endl;
	m_os << "09E0             : " << setw(16) << b->unk_9E0 << endl;
	for (k = 0; k < 0x10; k++)
		m_os << setw(4) << (0x9E8 + 2 * k) << "             : " << setw(4) << b->unk_9E8[k] << endl;
	for (k = 0; k < 0xBF && b->bid_bmp_hdr_list[k] != 0; k++)
		m_os << setw(4) << (0xA08 + 8 * k) << "cib_oid      : " << setw(16) << b->bid_bmp_hdr_list[k] << endl;

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

void BlockDumper::DumpBTNode_0()
{
	const APFS_ObjHeader * const hdr = reinterpret_cast<const APFS_ObjHeader *>(m_block);

	switch (hdr->o_subtype)
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
			typestr = "Purgatory B*-Tree (?)";
			break;
		case 0x0000000B:
			typestr = "btn / om / Mapping Node-ID => Block-ID";
			break;
		case 0x0000000E:
			typestr = "btn / apfs_root / Directory";
			break;
		case 0x0000000F:
			typestr = "btn / apfs_extentref / Mapping Block-ID => Object-ID";
			break;
		case 0x00000010:
			typestr = "btn / apfs_snap_meta / Snapshot Metadata";
			break;
		case 0x00000013:
			typestr = "oms / (?)";
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
		typestr = "om / (?)";
		break;
	case 0x000C:
		typestr = "cpm / Checkpoint Map (?)";
		break;
	case 0x000D:
		typestr = "apfs / Volume Superblock (APSB)";
		break;
	case 0x0011:
		typestr = "nr / (?)";
		break;
	case 0x12:
		typestr = "nrl / (?)";
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

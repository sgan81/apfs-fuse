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

#include <string>
#include <vector>
#include <ostream>

#include "Global.h"
#include "DiskStruct.h"

class BlockDumper
{
public:
	BlockDumper(std::ostream &os, size_t blocksize);
	~BlockDumper();

	void SetTextFlags(uint32_t flags) { m_text_flags = flags; }
	void DumpNode(const byte_t *block, uint64_t blk_nr);

	std::ostream &st() { return m_os; }

private:
	typedef void(BlockDumper::*DumpFunc)(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);

	void DumpNodeHeader(const APFS_BlockHeader *blk, uint64_t blk_nr);
	void DumpBTNode(DumpFunc func, uint16_t key_size = 0, uint16_t value_size = 0);
	void DumpBTHeader(bool dump_offsets = false);
	void DumpBTFooter();
	void DumpTableHeader(const APFS_TableHeader &tbl);

	void DumpBlk_0_D();
	void DumpBlk_4_7();
	void DumpBlk_4_B();
	void DumpBlk_4_C();
	void DumpBlk_8_1();
	void DumpBlk_8_5();
	void DumpBlk_8_11();

	void DumpBTNode_0();
	void DumpBTNode_4();
	void DumpBTNode_8();

	void DumpBTEntry_0_E(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);
	void DumpBTEntry_4_B(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);
	void DumpBTEntry_4_F(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);
	void DumpBTEntry_4_10(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);
	void DumpBTEntry_4_13(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);
	void DumpBTEntry_8_9(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);

	void DumpBTEntry_Unk(const byte_t *key_data, size_t key_length, const byte_t *value_data, size_t value_length, bool index);

	void DumpBlockHex();
	void DumpHex(const byte_t *data, size_t size, size_t line_size = 16);

public:
	static const char * GetNodeType(uint32_t type, uint32_t subtype);

private:
	static std::string tstamp(uint64_t apfs_time);

	uint32_t m_text_flags; // 00 - Alt, 01 - insensitive, 08 - sensitive

	std::ostream &m_os;
	const byte_t *m_block;
	const size_t m_blocksize;
};

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

#include <cstddef>
#include <cstdint>

class Inflate
{
public:
	Inflate();
	~Inflate();

	size_t Decompress(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size);

	size_t GetUsedInputBytes() const { return buffer_ptr; }

private:
	unsigned int GetBit();
	unsigned int GetBits(int bits);
	unsigned int DecodeValue(const unsigned short *hcnt, const unsigned short *hval);
	void CreateHuffmanTable(unsigned short *count, unsigned short count_sz, unsigned short *value, unsigned short value_sz, const unsigned char *sym_size);
	void CreateStaticTables();
	void ReadDynamicTables();
	void DecodeData();

	const uint8_t *buffer;
	size_t buffer_size;
	size_t buffer_ptr;

	uint8_t *dbuffer;
	size_t dbuffer_size;
	size_t dbuffer_ptr;

	uint8_t bit_buffer = 0;
	uint8_t bit_pos = 8;

	uint16_t lit_len_count[16];
	uint16_t lit_len_value[288];
	uint16_t dist_count[16];
	uint16_t dist_value[32];
	uint16_t clen_count[8];
	uint16_t clen_value[19];
};

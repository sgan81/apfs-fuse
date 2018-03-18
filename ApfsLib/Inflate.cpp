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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include "Inflate.h"

Inflate::Inflate()
{

}

Inflate::~Inflate()
{

}

unsigned int Inflate::GetBit()
{
	unsigned int bit;

	if (bit_pos >= 8)
	{
		bit_buffer = buffer[buffer_ptr++];
		bit_pos -= 8;
	}

	bit = bit_buffer & 1;
	bit_pos++;
	bit_buffer >>= 1;

	return bit;
}

unsigned int Inflate::GetBits(int bits)
{
	unsigned int v = 0;
	int i;

	for (i = 0; i < bits; i++)
		v = v | (GetBit() << i);

	return v;
}

unsigned int Inflate::DecodeValue(const unsigned short *hcnt, const unsigned short *hval)
{
	unsigned int b;
	int p;
	int i;

	p = 0;
	i = 1;
	b = 0;

	while (1)
	{
		b = (b << 1) | GetBit();
		if (b < hcnt[i])
			return hval[b + p];

		p += hcnt[i];
		b -= hcnt[i];
		i++;
	}
}

void Inflate::CreateHuffmanTable(unsigned short *count, unsigned short count_sz, unsigned short *value, unsigned short value_sz, const unsigned char *sym_size)
{
	unsigned short i, j;
	unsigned int sym_pos;

	for (i = 0; i < count_sz; i++)
		count[i] = 0;

	for (i = 0; i < value_sz; i++)
		value[i] = 0;

	sym_pos = 0;

	for (i = 1; i < count_sz; i++)
	{
		for (j = 0; j < value_sz; j++)
		{
			if (sym_size[j] == i)
			{
				count[i] ++;
				value[sym_pos] = j;
				sym_pos++;
			}
		}
	}
}

void Inflate::CreateStaticTables()
{
	unsigned char sym_size[288];
	int i;

	for (i = 0; i <= 143; i++)
		sym_size[i] = 8;

	for (i = 144; i <= 255; i++)
		sym_size[i] = 9;

	for (i = 256; i <= 279; i++)
		sym_size[i] = 7;

	for (i = 280; i <= 287; i++)
		sym_size[i] = 8;

	CreateHuffmanTable(lit_len_count, 16, lit_len_value, 288, sym_size);

	for (i = 0; i < 31; i++)
		sym_size[i] = 5;

	CreateHuffmanTable(dist_count, 16, dist_value, 32, sym_size);
}

void Inflate::ReadDynamicTables()
{
	unsigned char sym_size[288];
	int hlit;
	int hdist;
	int hclen;
	int i;
	unsigned int v;
	unsigned int len;

	static const unsigned short values[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

	hlit = GetBits(5) + 257;
	hdist = GetBits(5) + 1;
	hclen = GetBits(4) + 4;

	for (i = 0; i < hclen; i++)
		sym_size[values[i]] = GetBits(3);

	for (; i < 19; i++)
		sym_size[values[i]] = 0;

	CreateHuffmanTable(clen_count, 8, clen_value, 19, sym_size);

	for (i = 0; i < hlit; i++)
	{
		v = DecodeValue(clen_count, clen_value);
		if (v < 16)
			sym_size[i] = v;
		else if (v == 16)
		{
			len = 3 + GetBits(2);
			for (; len > 0; len--)
			{
				sym_size[i] = sym_size[i - 1];
				i++;
			}
			i--;
		}
		else
		{
			if (v == 17)
				len = 3 + GetBits(3);

			if (v == 18)
				len = 11 + GetBits(7);

			for (; len > 0; len--)
				sym_size[i++] = 0;

			i--;
		}
	}

	for (; i < 288; i++)
		sym_size[i] = 0;

	CreateHuffmanTable(lit_len_count, 16, lit_len_value, 288, sym_size);

	for (i = 0; i < hdist; i++)
	{
		v = DecodeValue(clen_count, clen_value);
		if (v < 16)
			sym_size[i] = v;
		else if (v == 16)
		{
			len = 3 + GetBits(2);
			for (; len > 0; len--)
			{
				sym_size[i] = sym_size[i - 1];
				i++;
			}
			i--;
		}
		else
		{
			if (v == 17)
				len = 3 + GetBits(3);

			if (v == 18)
				len = 11 + GetBits(7);

			for (; len > 0; len--)
				sym_size[i++] = 0;

			i--;
		}
	}

	for (; i < 32; i++)
		sym_size[i] = 0;

	CreateHuffmanTable(dist_count, 16, dist_value, 32, sym_size);
}

void Inflate::DecodeData()
{
	unsigned int v;
	unsigned int exbits;
	unsigned int length;
	unsigned int distance;

	static const int lengths[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const int distances[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };

	while (true)
	{
		v = DecodeValue(lit_len_count, lit_len_value);

		if (v < 256)
			dbuffer[dbuffer_ptr++] = v;
		else if (v == 256)
			return;
		else
		{
			length = lengths[v - 257];
			if (v >= 265 && v < 285)
			{
				exbits = (v - 261) >> 2;
				length = length + GetBits(exbits);
			}
			v = DecodeValue(dist_count, dist_value);

			distance = distances[v];
			if (v >= 4)
			{
				exbits = (v - 2) >> 1;
				distance = distance + GetBits(exbits);
			}

			for (v = 0; v < length; v++)
			{
				dbuffer[dbuffer_ptr] = dbuffer[dbuffer_ptr - distance];
				dbuffer_ptr++;
			}
		}
	}
}

size_t Inflate::Decompress(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
	int bfinal;
	int btype;
	int len;
	// int nlen;

	buffer = src;
	buffer_size = src_size;
	buffer_ptr = 0;

	dbuffer = dst;
	dbuffer_size = dst_size;
	dbuffer_ptr = 0;

	do
	{
		bfinal = GetBit();
		btype = GetBits(2);

		switch (btype)
		{
		case 0:
			len = buffer[buffer_ptr] | (buffer[buffer_ptr + 1] << 8);
			// nlen = buffer[buffer_ptr + 2] | (buffer[buffer_ptr + 3] << 8);
			buffer_ptr += 4;
			bit_pos = 8;
			memcpy(dbuffer + dbuffer_ptr, buffer + buffer_ptr, len);
			dbuffer_ptr += len;
			buffer_ptr += len;
			break;

		case 1:
			CreateStaticTables();
			DecodeData();
			break;

		case 2:
			ReadDynamicTables();
			DecodeData();
			break;

		default:
			assert(false);
			bfinal = true;
			break;
		}
	} while (!bfinal);

	return dbuffer_ptr;
}

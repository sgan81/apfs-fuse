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

#include "Crc32.h"

Crc32::Crc32(bool reflect, uint32_t poly)
{
	unsigned int i;
	uint32_t r;
	unsigned int b;

	m_reflect = reflect;
	m_crc = 0;

	if (reflect) {
		poly = ((poly << 16) & 0xFFFF0000) | ((poly >> 16) & 0x0000FFFF);
		poly = ((poly << 8) & 0xFF00FF00) | ((poly >> 8) & 0x00FF00FF);
		poly = ((poly << 4) & 0xF0F0F0F0) | ((poly >> 4) & 0x0F0F0F0F);
		poly = ((poly << 2) & 0xCCCCCCCC) | ((poly >> 2) & 0x33333333);
		poly = ((poly << 1) & 0xAAAAAAAA) | ((poly >> 1) & 0x55555555);

		for (i = 0; i < 256; i++) {
			r = i;
			for (b = 0; b < 8; b++) {
				if (r & 1)
					r = (r >> 1) ^ poly;
				else
					r = (r >> 1);
			}
			m_table[i] = r;
		}
	}
	else {
		for (i = 0; i < 256; i++) {
			r = (i << 24);
			for (b = 0; b < 8; b++) {
				if (r & 0x80000000)
					r = (r << 1) ^ poly;
				else
					r = (r << 1);
			}
			m_table[i] = r;
		}
	}
}

Crc32::~Crc32()
{

}

void Crc32::Calc(const byte_t *data, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (m_reflect) {
			CalcLE(data[i]);
		}
		else {
			CalcBE(data[i]);
		}
	}
}

void Crc32::CalcLE(byte_t b)
{
	m_crc = m_table[b ^ (m_crc & 0xFF)] ^ (m_crc >> 8);
}

void Crc32::CalcBE(byte_t b)
{
	m_crc = m_table[b ^ ((m_crc >> 24) & 0xFF)] ^ (m_crc << 8);
}

uint32_t Crc32::GetDataCRC(const byte_t *data, size_t size, uint32_t initialXor, uint32_t finalXor)
{
	m_crc = initialXor;
	Calc(data, size);
	return m_crc ^ finalXor;
}

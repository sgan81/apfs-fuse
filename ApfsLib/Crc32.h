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
#include "Global.h"

class Crc32
{
public:
	Crc32(bool reflect, uint32_t poly = 0x04C11DB7);
	~Crc32();

	void SetCRC(uint32_t crc) { m_crc = crc; }
	uint32_t GetCRC() const { return m_crc; }
	void Calc(const byte_t *data, size_t size);

	uint32_t GetDataCRC(const byte_t *data, size_t size, uint32_t initialXor, uint32_t finalXor);

private:
	void CalcLE(byte_t b);
	void CalcBE(byte_t b);

	uint32_t m_table[256];
	uint32_t m_crc;

	bool m_reflect;
};


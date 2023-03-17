/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <vector>

#include "Endian.h"
#include "Crc32.h"

class Device;

struct PMAP_GptHeader;
struct PMAP_Entry;

class GptPartitionMap
{
public:
	GptPartitionMap();

	bool LoadAndVerify(Device &dev);

	int FindFirstAPFSPartition();
	bool GetPartitionOffsetAndSize(int partnum, uint64_t &offset, uint64_t &size);

	void ListEntries();

private:
	Crc32 m_crc;

	std::vector<uint8_t> m_hdr_data;
	std::vector<uint8_t> m_entry_data;

	const PMAP_GptHeader *m_hdr;
	const PMAP_Entry *m_map;
	unsigned int m_sector_size;
};

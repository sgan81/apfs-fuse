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
#include <fstream>
#include <string>
#include <vector>

#include "Device.h"
#include "DiskImageFile.h"

#include "Crc32.h"

#undef DMG_DEBUG
#define DMG_CACHE

class DeviceDMG : public Device
{
	struct DmgSection
	{
		DmgSection();
		~DmgSection();

		uint32_t method;
		uint32_t comment;
		uint64_t disk_offset;
		uint64_t disk_length;
		uint64_t dmg_offset;
		uint64_t dmg_length;
		uint8_t *cache;
	};

public:
	DeviceDMG();
	~DeviceDMG();

	bool Open(const char *name) override;
	void Close() override;

	bool Read(void *data, uint64_t offs, uint64_t len) override;
	uint64_t GetSize() const override;

private:
	bool ProcessHeaderXML(uint64_t off, uint64_t size);
	bool ProcessHeaderRsrc(uint64_t off, uint64_t size);

	void ProcessMish(const uint8_t *data, size_t size);

	DiskImageFile m_img;
	uint64_t m_size;

	bool m_is_raw;

	Crc32 m_crc;

	std::vector<DmgSection> m_sections;

#ifdef DMG_DEBUG
	std::ofstream m_dbg;
#endif
#ifdef DMG_CACHE
	uint64_t m_cache_base;
	uint64_t m_cache_size;
	uint8_t *m_cache_data;
#endif
};

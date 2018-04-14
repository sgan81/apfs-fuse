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

#include "Endian.h"
#include "Crc32.h"

#include "Device.h"

class DeviceDMG : public Device
{
#pragma pack(push)
#pragma pack(1)
	struct KolyHeader
	{
		be<uint32_t> signature;
		be<uint32_t> version;
		be<uint32_t> headersize;
		be<uint32_t> flags;
		be<uint64_t> running_data_fork_offset;
		be<uint64_t> data_fork_offset;
		be<uint64_t> data_fork_length;
		be<uint64_t> rsrc_fork_offset;
		be<uint64_t> rsrc_fork_length;
		be<uint32_t> segment_number;
		be<uint32_t> segment_count;
		be<uint8_t>  segment_id[0x10];
		// 0x50
		be<uint32_t> data_fork_checksum_type;
		be<uint32_t> data_fork_unknown;
		be<uint32_t> data_fork_checksum_data;
		be<uint8_t>  unk_05C[0x7C];
		// 0xD8
		be<uint64_t> xml_offset;
		be<uint64_t> xml_length;
		// ???
		// uint64_t codesign_offset;
		// uint64_t codesign_length;
		be<uint8_t>  unk_0E8[0x78];
		// 0x160
		be<uint32_t> master_checksum_type;
		be<uint32_t> master_checksum_unknown;
		be<uint32_t> master_checksum_data;
		be<uint8_t>  unk_16C[0x7C];
		// 0x1E8
		be<uint32_t> image_variant;
		be<uint64_t> sector_count;
		// 0x1F4
		be<uint8_t>  unk_1F4[0x0C];
	};

	static_assert(sizeof(KolyHeader) == 0x200, "Wrong Koly Header Size");

	struct MishHeader
	{
		be<uint32_t> signature;
		be<uint32_t> unk;
		be<uint64_t> sector_start;
		be<uint64_t> sector_count;
		be<uint64_t> unk_18;
		be<uint32_t> unk_20;
		be<uint32_t> part_id;
		be<uint8_t>  unk_28[0x18];
		be<uint32_t> checksum_type;
		be<uint32_t> checksum_unk;
		be<uint32_t> checksum_data;
		be<uint8_t>  unk_4C[0x7C];
		be<uint32_t> entry_count;
	};

	static_assert(sizeof(MishHeader) == 0xCC, "Wrong Mish Header Size");

	struct MishEntry
	{
		be<uint32_t> method;
		be<uint32_t> unk;
		be<uint64_t> sector_start;
		be<uint64_t> sector_count;
		be<uint64_t> dmg_offset;
		be<uint64_t> dmg_length;
	};

	static_assert(sizeof(MishEntry) == 0x28, "Wrong Mish Entry Size");

#pragma pack(pop)

	struct DmgSection
	{
		DmgSection();
		~DmgSection();

		uint32_t method;
		uint32_t last_access;
		uint64_t drive_offset;
		uint64_t drive_length;
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
	void Base64Decode(std::vector<uint8_t> &bin, const char *str, size_t size);

	std::ifstream m_dmg;
	uint64_t m_size;
	Crc32 m_crc;

	std::vector<DmgSection> m_sections;
};

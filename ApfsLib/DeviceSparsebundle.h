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
#include "Aes.h"

#include "DeviceImageDisk.h"

#undef DMG_DEBUG

class DeviceSparsebundle : public DeviceImageDisk
{
public:
	DeviceSparsebundle();
	~DeviceSparsebundle();

	virtual bool Open(const char *name);
	virtual void Close();

	virtual void ReadRaw(void* data, size_t size, off_t off);

	virtual bool Read(void *data, uint64_t offs, uint64_t len);
	uint64_t GetSize() const;

private:
    char* m_path;
	char* m_band_path;
	char* m_band_path_band_number_start;

    size_t m_band_size;
    size_t m_blocksize;
    off_t m_size;
    off_t m_opened_file_band_number;
    int m_opened_file_fd;

};

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

#include "Device.h"

#undef DMG_DEBUG

class DeviceImageDisk : public Device
{
public:
	DeviceImageDisk();
	~DeviceImageDisk();

	virtual void ReadRaw(void* data, size_t size, off_t off) = 0;
	void ReadInternal(uint64_t off, void *data, size_t size);

	bool SetupEncryptionV2(std::ifstream& m_dmg);

protected:
	AES m_aes;
	bool m_is_encrypted;
	uint64_t m_crypt_offset;
	uint64_t m_crypt_size;
	uint32_t m_crypt_blocksize;
	uint8_t m_hmac_key[0x14];
};

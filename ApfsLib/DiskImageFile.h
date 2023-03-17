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
#include <fstream>

#include <Crypto/Aes.h>
#include "Device.h"

class DiskImageFile
{
public:
	DiskImageFile();
	~DiskImageFile();

	bool Open(const char *name);
	void Close();
	void Reset();

	void Read(uint64_t off, void *data, size_t size);

	uint64_t GetContentSize() const { return m_crypt_size; }

	bool CheckSetupEncryption();

private:
	bool SetupEncryptionV1();
	bool SetupEncryptionV2();
	size_t PkcsUnpad(const uint8_t *data, size_t size);

	std::ifstream m_image;

	bool m_is_encrypted;
	uint64_t m_crypt_offset;
	uint64_t m_crypt_size;
	uint32_t m_crypt_blocksize;
	uint8_t m_hmac_key[0x14];

	AES m_aes;
};

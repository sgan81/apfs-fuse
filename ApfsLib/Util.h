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
#include <string>
#include <ostream>
#include <vector>

#include "Global.h"

uint64_t Fletcher64(const uint32_t *data, size_t cnt, uint64_t init);
bool VerifyBlock(const void *block, size_t size);
bool IsZero(const byte_t *data, size_t size);
bool IsEmptyBlock(const void *data, size_t blksize);
void DumpHex(std::ostream &os, const byte_t *data, size_t size, size_t line_size = 16);
void DumpBuffer(const uint8_t *data, size_t len, const char *label);
std::string uuidstr(const apfs_uuid_t &uuid);
uint32_t HashFilename(const char *utf8str, uint16_t name_len, bool case_insensitive);
bool GetPassword(std::string &pw);
bool Utf8toU32(std::vector<char32_t>& u32_str, const uint8_t * str);

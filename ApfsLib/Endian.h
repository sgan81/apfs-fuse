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
/*
This file is for handling the different endiannesses used by Apple.
Also helps making the driver run on big-endian architectures.
*/
#pragma once

#include <cstdint>

// This should later be done by some configuration ...
#define APFS_LITTLE_ENDIAN
#undef APFS_BIG_ENDIAN

#ifdef _MSC_VER
// Definitions for Visual Studio
#include <intrin.h>

#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)
#endif
#ifdef __linux__
// Definitions for Linux
#include <byteswap.h>
#endif
#ifdef __APPLE__
// Definitions for macOS
#include <libkern/OSByteOrder.h>
#define bswap_16(x) _OSSwapInt16(x)
#define bswap_32(x) _OSSwapInt32(x)
#define bswap_64(x) _OSSwapInt64(x)
#endif

#ifdef APFS_LITTLE_ENDIAN
// Swap to/from little endian.
inline uint16_t bswap_le(const uint16_t &x) { return x; }
inline uint32_t bswap_le(const uint32_t &x) { return x; }
inline uint64_t bswap_le(const uint64_t &x) { return x; }
// Swap to/from big endian.
inline uint16_t bswap_be(const uint16_t &x) { return bswap_16(x); }
inline uint32_t bswap_be(const uint32_t &x) { return bswap_32(x); }
inline uint64_t bswap_be(const uint64_t &x) { return bswap_64(x); }
#endif
#ifdef APFS_BIG_ENDIAN
// Swap to/from little endian.
inline uint16_t bswap_le(const uint16_t &x) { return bswap_16(x); }
inline uint32_t bswap_le(const uint32_t &x) { return bswap_32(x); }
inline uint64_t bswap_le(const uint64_t &x) { return bswap_64(x); }
// Swap to/from big endian.
inline uint16_t bswap_be(const uint16_t &x) { return x; }
inline uint32_t bswap_be(const uint32_t &x) { return x; }
inline uint64_t bswap_be(const uint64_t &x) { return x; }
#endif

inline uint8_t bswap_le(const uint8_t &x) { return x; }
inline uint8_t bswap_be(const uint8_t &x) { return x; }

// Big-endian template that does automatic swapping if necessary.
template<typename T>
struct be
{
public:
	void operator=(T v) { val = bswap_be(v); }
	operator T() const { return bswap_be(val); }
	T get() const { return bswap_be(val); }
private:
	T val;
};

// Little-endian template that does automatic swapping if necessary.
template<typename T>
struct le
{
public:
	void operator=(T v) { val = bswap_le(v); }
	operator T() const { return bswap_le(val); }
	T get() const { return bswap_le(val); }
private:
	T val;
};

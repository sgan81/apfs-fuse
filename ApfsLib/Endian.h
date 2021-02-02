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

#define be16toh(x) _byteswap_ushort(x)
#define be32toh(x) _byteswap_ulong(x)
#define be64toh(x) _byteswap_uint64(x)
#define htobe16(x) _byteswap_ushort(x)
#define htobe32(x) _byteswap_ulong(x)
#define htobe64(x) _byteswap_uint64(x)

#define le16toh(x) (x)
#define le32toh(x) (x)
#define le64toh(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
#define htole64(x) (x)

#define __attribute__(x)

#endif
#ifdef __linux__
// Definitions for Linux
#include <byteswap.h>
#include <endian.h>
#endif
#ifdef __FreeBSD__
#include <sys/endian.h>

#define bswap_16 bswap16
#define bswap_32 bswap32
#define bswap_64 bswap64

#endif
#ifdef __APPLE__
// Definitions for macOS
#include <libkern/OSByteOrder.h>
#define bswap_16(x) _OSSwapInt16(x)
#define bswap_32(x) _OSSwapInt32(x)
#define bswap_64(x) _OSSwapInt64(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#endif

#ifdef APFS_LITTLE_ENDIAN
// Swap to/from little endian.
inline int16_t bswap_le(const int16_t &x) { return x; }
inline int32_t bswap_le(const int32_t &x) { return x; }
inline int64_t bswap_le(const int64_t &x) { return x; }
inline uint16_t bswap_le(const uint16_t &x) { return x; }
inline uint32_t bswap_le(const uint32_t &x) { return x; }
inline uint64_t bswap_le(const uint64_t &x) { return x; }
// Swap to/from big endian.
inline int16_t bswap_be(const int16_t& x) { return bswap_16(x); }
inline int32_t bswap_be(const int32_t& x) { return bswap_32(x); }
inline int64_t bswap_be(const int64_t& x) { return bswap_64(x); }
inline uint16_t bswap_be(const uint16_t& x) { return bswap_16(x); }
inline uint32_t bswap_be(const uint32_t& x) { return bswap_32(x); }
inline uint64_t bswap_be(const uint64_t& x) { return bswap_64(x); }

typedef uint8_t le_uint8_t;
typedef uint16_t le_uint16_t;
typedef uint32_t le_uint32_t;
typedef uint64_t le_uint64_t;
typedef int8_t le_int8_t;
typedef int16_t le_int16_t;
typedef int32_t le_int32_t;
typedef int64_t le_int64_t;
typedef uint8_t be_uint8_t;
struct be_uint16_t {
	void operator=(uint16_t v) { val = bswap_16(v); }
	operator uint16_t() const { return bswap_16(val); }
	uint16_t get() const { return bswap_16(val); }
private:
	uint16_t val;
} __attribute__((packed));
struct be_uint32_t {
	void operator=(uint32_t v) { val = bswap_32(v); }
	operator uint32_t() const { return bswap_32(val); }
	uint32_t get() const { return bswap_32(val); }
private:
	uint32_t val;
} __attribute__((packed));
struct be_uint64_t {
	void operator=(uint64_t v) { val = bswap_64(v); }
	operator uint64_t() const { return bswap_64(val); }
	uint64_t get() const { return bswap_64(val); }
private:
	uint64_t val;
} __attribute__((packed));

#endif

#ifdef APFS_BIG_ENDIAN
// Swap to/from little endian.
inline int16_t bswap_le(const int16_t &x) { return bswap_16(x); }
inline int32_t bswap_le(const int32_t &x) { return bswap_32(x); }
inline int64_t bswap_le(const int64_t &x) { return bswap_64(x); }
inline uint16_t bswap_le(const uint16_t &x) { return bswap_16(x); }
inline uint32_t bswap_le(const uint32_t &x) { return bswap_32(x); }
inline uint64_t bswap_le(const uint64_t &x) { return bswap_64(x); }
// Swap to/from big endian.
inline int16_t bswap_be(const int16_t &x) { return x; }
inline int32_t bswap_be(const int32_t &x) { return x; }
inline int64_t bswap_be(const int64_t &x) { return x; }
inline uint16_t bswap_be(const uint16_t &x) { return x; }
inline uint32_t bswap_be(const uint32_t &x) { return x; }
inline uint64_t bswap_be(const uint64_t &x) { return x; }

typedef uint8_t le_uint8_t;
struct le_uint16_t {
	void operator=(uint16_t v) { val = bswap_16(v); }
	operator uint16_t() const { return bswap_16(val); }
	uint16_t get() const { return bswap_16(val); }
private:
	uint16_t val;
} __attribute__((packed));
struct le_uint32_t {
	void operator=(uint32_t v) { val = bswap_32(v); }
	operator uint32_t() const { return bswap_32(val); }
	uint32_t get() const { return bswap_32(val); }
private:
	uint32_t val;
} __attribute__((packed));
struct le_uint64_t {
	void operator=(uint64_t v) { val = bswap_64(v); }
	operator uint64_t() const { return bswap_64(val); }
	uint64_t get() const { return bswap_64(val); }
private:
	uint64_t val;
} __attribute__((packed));
struct le_int64_t {
	void operator=(int64_t v) { val = bswap_64(v); }
	operator int64_t() const { return bswap_64(val); }
	int64_t get() const { return bswap_64(val); }
private:
	int64_t val;
} __attribute__((packed));

typedef uint8_t be_uint8_t;
typedef uint16_t be_uint16_t;
typedef uint32_t be_uint32_t;
typedef uint64_t be_uint64_t;
#endif

#if 0

inline int8_t bswap_le(const int8_t &x) { return x; }
inline int8_t bswap_be(const int8_t &x) { return x; }
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

#endif

#ifdef _MSC_VER
#undef __attribute__
#endif

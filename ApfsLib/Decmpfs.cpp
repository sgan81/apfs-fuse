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

#include <iomanip>
#include <iostream>
#include <cstring>
#include <cassert>

#include "Decmpfs.h"
#include "Endian.h"

#include "Global.h"
#include "Util.h"
#include "Debug.h"

enum ChunkScheme {
	CS_UNKNOWN,
	CS_EMBEDDED,
	CS_RSRC_FORK,
	CS_RSRC_DATA
};

enum CompressionMethod {
	CM_UNKNOWN,
	CM_UNCOMPRESSED,
	CM_ZLIB,
	CM_LZVN,
	CM_LZFSE,
	CM_LZBITMAP
};

struct CmpfMethod {
	ChunkScheme scheme;
	CompressionMethod method;
};

static const CmpfMethod cmpf_list[16] = {
	{ CS_UNKNOWN, CM_UNKNOWN },
	{ CS_EMBEDDED, CM_UNCOMPRESSED },
	{ CS_UNKNOWN, CM_UNKNOWN },
	{ CS_EMBEDDED, CM_ZLIB },
	{ CS_RSRC_FORK, CM_ZLIB },
	{ CS_UNKNOWN, CM_UNKNOWN },
	{ CS_UNKNOWN, CM_UNKNOWN },
	{ CS_EMBEDDED, CM_LZVN },
	{ CS_RSRC_DATA, CM_LZVN },
	{ CS_EMBEDDED, CM_UNCOMPRESSED }, // Not sure ...
	{ CS_RSRC_DATA, CM_UNCOMPRESSED },
	{ CS_EMBEDDED, CM_LZFSE },
	{ CS_RSRC_DATA, CM_LZFSE },
	{ CS_EMBEDDED, CM_LZBITMAP },
	{ CS_RSRC_DATA, CM_LZBITMAP },
	{ CS_UNKNOWN, CM_UNKNOWN }
};

struct RsrcForkHeader
{
	be_uint32_t data_offset;
	be_uint32_t mgmt_offset;
	be_uint32_t data_size;
	be_uint32_t mgmt_size;
};

struct CmpfRsrcEntry
{
	// 1 64K-Block
	le_uint32_t off;
	le_uint32_t size;
};

struct CmpfRsrc
{
	le_uint32_t entries;
	CmpfRsrcEntry entry[32];
};

bool IsDecompAlgoSupported(uint16_t algo)
{
	return cmpf_list[algo].scheme != CS_UNKNOWN && cmpf_list[algo].method != CM_UNKNOWN;
}

bool IsDecompAlgoInRsrc(uint16_t algo)
{
	return cmpf_list[algo].scheme == CS_RSRC_FORK || cmpf_list[algo].scheme == CS_RSRC_DATA;
}

bool DecompressFile(ApfsDir &dir, uint64_t ino, std::vector<uint8_t> &decompressed, const std::vector<uint8_t> &compressed)
{
	if (compressed.size() < sizeof(CompressionHeader))
		return false;

	const CompressionHeader *hdr = reinterpret_cast<const CompressionHeader *>(compressed.data());
	const uint8_t *cdata = compressed.data() + sizeof(CompressionHeader);
	size_t csize = compressed.size() - sizeof(CompressionHeader);
	size_t decoded_bytes = 0;

#if 1 // Disable to get compressed data
	if (g_debug & Dbg_Cmpfs)
	{
		std::cout << "DecompressFile " << compressed.size() << " => " << hdr->size << ", algo = " << hdr->algo;

		switch (hdr->algo)
		{
		case 3: std::cout << " (Zlib, Attr)"; break;
		case 4: std::cout << " (Zlib, Rsrc)"; break;
		case 7: std::cout << " (LZVN, Attr)"; break;
		case 8: std::cout << " (LZVN, Rsrc)"; break;
		case 9: std::cout << " (Uncompressed, Attr)"; break;
		case 10: std::cout << " (Uncompressed, Rsrc)"; break;
		case 11: std::cout << " (LZFSE, Attr)"; break;
		case 12: std::cout << " (LZFSE, Rsrc)"; break;
		case 13: std::cout << " (LZBITMAP, Attr)"; break;
		case 14: std::cout << " (LZBITMAP, Rsrc)"; break;
		default: std::cout << " (Unknown)"; break;
		}

		std::cout << std::endl;
	}

	if (!IsDecompAlgoSupported(hdr->algo))
	{
		if (g_debug & Dbg_Errors) {
			std::cout << "Unsupported decompression algorithm." << std::endl;
			DumpHex(std::cout, compressed.data(), compressed.size());
		}
		return false;
	}

	if (IsDecompAlgoInRsrc(hdr->algo))
	{
		std::vector<uint8_t> rsrc;
		size_t k;

		bool rc = dir.GetAttribute(rsrc, ino, "com.apple.ResourceFork");

		if (!rc)
		{
			if (g_debug & Dbg_Errors)
				std::cout << "Decmpfs: Could not find resource fork " << ino << std::endl;
			decompressed.clear();
			return false;
		}

		if (hdr->algo == 4) // Zlib, rsrc
		{
			RsrcForkHeader rsrc_hdr;

			memcpy(&rsrc_hdr, rsrc.data(), sizeof(rsrc_hdr));

			if (rsrc_hdr.data_offset > rsrc.size())
			{
				if (g_debug & Dbg_Errors)
					std::cout << "Decmpfs: Invalid data offset in rsrc header." << std::endl;
				return false;
			}

			const uint8_t *cmpf_rsrc_base = rsrc.data() + rsrc_hdr.data_offset + sizeof(uint32_t);
			const CmpfRsrc *cmpf_rsrc = reinterpret_cast<const CmpfRsrc *>(cmpf_rsrc_base);

			decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

			for (k = 0; k < cmpf_rsrc->entries; k++)
			{
				size_t src_offset = cmpf_rsrc->entry[k].off;
				const uint8_t *src = cmpf_rsrc_base + src_offset;
				size_t src_len = cmpf_rsrc->entry[k].size;
				uint8_t *dst = decompressed.data() + 0x10000 * k;
				size_t expected_len = hdr->size - (0x10000 * k);
				if (expected_len > 0x10000)
					expected_len = 0x10000;

				if (src_len > 0x10001)
				{
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: In rsrc, src_len too big (" << src_len << ")" << std::endl;
					return false;
				}

				if (src[0] == 0x78)
				{
					decoded_bytes = DecompressZLib(dst, 0x10000, src, src_len);
				}
				else if ((src[0] & 0x0F) == 0x0F)
				{
					memcpy(dst, src + 1, src_len - 1);
					decoded_bytes = src_len - 1;
				}
				else
				{
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: Something wrong with zlib data." << std::endl;
					decompressed.clear();
					return false;
				}

				if (expected_len != decoded_bytes)
				{
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: Expected len != decompressed len: " << expected_len << " != " << decoded_bytes << std::endl;
					return false;
				}
			}
		}
		else
		{
			const uint32_t *off_list = reinterpret_cast<const uint32_t *>(rsrc.data());

			decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

			for (k = 0; (k << 16) < decompressed.size(); k++) {
				size_t expected_len = hdr->size - (0x10000 * k);
				if (expected_len > 0x10000)
					expected_len = 0x10000;
				const uint8_t *src = rsrc.data() + off_list[k];
				size_t src_len = off_list[k + 1] - off_list[k];

				if (src_len > 0x10001) {
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: In rsrc, src_len too big (" << src_len << ")" << std::endl;
					return false;
				}

				switch (hdr->algo) {
				case 8:
					if (src[0] == 0x06) {
						memcpy(decompressed.data() + (k << 16), src + 1, src_len - 1);
						decoded_bytes = src_len - 1;
					}
					else
						decoded_bytes = DecompressLZVN(decompressed.data() + (k << 16), expected_len, src, src_len);
					break;
				case 10:
					// Assuming ...
					memcpy(decompressed.data() + (k << 16), src + 1, src_len - 1);
					decoded_bytes = src_len - 1;
					break;
				case 12:
					// Assuming ...
					decoded_bytes = DecompressLZFSE(decompressed.data() + (k << 16), expected_len, src, src_len);
					// TODO is there also an uncompressed variant?
					break;
				case 14:
					if (src[0] == 0xFF) {
						memcpy(decompressed.data() + (k << 16), src + 1, src_len - 1);
						decoded_bytes = src_len - 1;
					}
					else
						decoded_bytes = DecompressLZBITMAP(decompressed.data() + (k << 16), expected_len, src, src_len);
					break;
				default:
					decoded_bytes = 0;
					break;
				}

				if (decoded_bytes != expected_len) {
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: Expected length != decompressed length: " << expected_len << " != " << decoded_bytes << " [k = " << k << "]" << std::endl;

					return false;
				}
			}
		}

		decompressed.resize(hdr->size);
	}
	else
	{
		decompressed.resize(hdr->size);

		switch (hdr->algo) {
		case 3:
			if (cdata[0] == 0x78)
				decoded_bytes = DecompressZLib(decompressed.data(), decompressed.size(), cdata, csize);
			else if (cdata[0] == 0xFF) {
				assert(hdr->size == csize - 1);
				decompressed.assign(cdata + 1, cdata + csize);
				decoded_bytes = decompressed.size();
			}
			else
				return false;
			break;

		case 7:
			if (cdata[0] == 0x06) {
				assert(hdr->size == csize - 1);
				decompressed.assign(cdata + 1, cdata + csize);
				decoded_bytes = decompressed.size();
			}
			else
				decoded_bytes = DecompressLZVN(decompressed.data(), decompressed.size(), cdata, csize);
			break;

		case 9:
			assert(cdata[0] == 0xCC);
			assert(hdr->size == csize - 1);
			decompressed.assign(cdata + 1, cdata + csize);
			decoded_bytes = decompressed.size();
			break;

		case 11:
			// TODO uncompressed variant?
			decoded_bytes = DecompressLZFSE(decompressed.data(), decompressed.size(), cdata, csize);
			break;

		case 13:
			if (cdata[0] == 0xFF) {
				assert(hdr->size == csize - 1);
				decompressed.assign(cdata + 1, cdata + csize);
				decoded_bytes = decompressed.size();
			}
			else
				decoded_bytes = DecompressLZBITMAP(decompressed.data(), decompressed.size(), cdata, csize);
			break;

		default:
			decoded_bytes = 0;
			break;
		}

		if (decoded_bytes != hdr->size)
		{
			if (g_debug & Dbg_Errors)
				std::cout << "Decmpfs: In attr, expected len != decoded len: " << hdr->size << " != " << decoded_bytes << std::endl;
			return false;
		}
	}

#else
	if (IsDecompAlgoInRsrc(hdr->algo))
	{
		dir.GetAttribute(decompressed, ino, "com.apple.ResourceFork");
	}
	else
	{
		decompressed = compressed;
	}
#endif

	return true;
}

Decmpfs::Decmpfs(ApfsDir& dir) : m_dir(dir)
{
}

Decmpfs::~Decmpfs()
{
}

int Decmpfs::open(uint64_t ino)
{
	std::vector<uint8_t> xattr;
	const CompressionHeader* chdr;

	m_dir.GetAttribute(xattr, ino, "com.apple.decmpfs");
	chdr = reinterpret_cast<const CompressionHeader*>(xattr.data());


	// Etc PP ...
}

int Decmpfs::pread(void* data, size_t size, uint64_t offset, uint64_t& nread)
{
}

int Decmpfs::close()
{
}

size_t Decmpfs::decompress(uint8_t* dst, size_t dst_size, const uint8_t* src, size_t src_size, int algo)
{
	size_t decoded = 0;

	switch (algo) {
	case CM_UNCOMPRESSED:
		if (dst_size < src_size) break;
		memcpy(dst, src, src_size);
		decoded = src_size;
		break;
	case CM_ZLIB:
		if (src[0] == 0x78)
			decoded = DecompressZLib(dst, dst_size, src, src_size);
		else if ((src[0] & 0x0F) == 0x0F) {
			if (dst_size < (src_size - 1)) break;
			memcpy(dst, src + 1, src_size - 1);
			decoded = src_size - 1;
		} else {
			decoded = 0;
		}
		break;
	case CM_LZVN:
		if (src[0] == 0x06) {
			if (dst_size < (src_size - 1)) break;
			memcpy(dst, src + 1, src_size - 1);
			decoded = src_size - 1;
		} else
			decoded = DecompressLZVN(dst, dst_size, src, src_size);
		break;
	case CM_LZFSE:
		// TODO uncompressed?
		decoded = DecompressLZFSE(dst, dst_size, src, src_size);
		break;
	case CM_LZBITMAP:
		if (src[0] == 0xFF) {
			if (dst_size < (src_size - 1)) break;
			memcpy(dst, src + 1, src_size - 1);
			decoded = src_size - 1;
		} else
			decoded = DecompressLZBITMAP(dst, dst_size, src, src_size);
		break;
	default:
		log_error("Unsupported decompression algorithm %d\n", algo);
		break;
	}

	if (decoded == 0) {
		log_error("decmpfs: something went wrong ... algo = %d\n", algo);
		dbg_dump_hex(src, 0x100);
	}

	return decoded;
}

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


struct RsrcForkHeader
{
	be<uint32_t> data_offset;
	be<uint32_t> mgmt_offset;
	be<uint32_t> data_size;
	be<uint32_t> mgmt_size;
};

struct CmpfRsrcEntry
{
	// 1 64K-Block
	le<uint32_t> off;
	le<uint32_t> size;
};

struct CmpfRsrc
{
	le<uint32_t> entries;
	CmpfRsrcEntry entry[32];
};

bool IsDecompAlgoSupported(uint16_t algo)
{
	switch (algo)
	{
	case 3:
	case 4:
	case 7:
	case 8:
		return true;
	default:
		return false;
	}
}

bool IsDecompAlgoInRsrc(uint16_t algo)
{
	switch (algo)
	{
	case 4:
	case 8:
		return true;
	default:
		return false;
	}
}

static size_t expected_block_len(int block_number, size_t uncompressed_file_size)
{
	const size_t uncompressed_block_standard_size = 0x10000;

	int whole_blocks = uncompressed_file_size >> 16;
	if (block_number < whole_blocks)
		return uncompressed_block_standard_size;

	if (block_number == whole_blocks)
		return uncompressed_file_size % uncompressed_block_standard_size;

	return 0;
}

bool DecompressFile(ApfsDir &dir, uint64_t ino, std::vector<uint8_t> &decompressed, const std::vector<uint8_t> &compressed)
{
#if 1
	if (compressed.size() < sizeof(CompressionHeader))
		return false;

	const CompressionHeader *hdr = reinterpret_cast<const CompressionHeader *>(compressed.data());
	const uint8_t *cdata = compressed.data() + sizeof(CompressionHeader);
	size_t csize = compressed.size() - sizeof(CompressionHeader);

	if (g_debug > 8)
		std::cout << "DecompressFile " << compressed.size() << " => " << hdr->size << ", algo = " << hdr->algo << std::endl;

	if (hdr->algo == 3)
	{
		decompressed.resize(hdr->size);

		if (compressed[0x10] == 0x78)
		{
			size_t decoded_bytes = DecompressZLib(decompressed.data(), decompressed.size(), cdata, csize);
			if (decoded_bytes != hdr->size)
			{
				if (g_debug > 0)
					std::cout << "Expected " << hdr->size << " bytes in compressed stream, "
						  << "got " << decoded_bytes << std::endl;
				if (!g_lax)
					return false;
			}
		}
		else if (compressed[0x10] == 0xFF)
		{
			decompressed.assign(cdata + 1, cdata + csize);
		}
	}
	else if (hdr->algo == 4)
	{
		std::vector<uint8_t> rsrc;

		if (g_debug > 8)
		{
			std::cout << "type=4: zlib in resource fork" << std::endl;
			std::cout << " stream info: size=" << compressed.size() << std::endl;
			DumpBuffer(compressed.data(), compressed.size(), "decmpfs content");
		}

		bool rc = dir.GetAttribute(rsrc, ino, "com.apple.ResourceFork");
		if (!rc)
		{
			if (g_debug > 0)
				std::cout << "Could not read resource fork" << std::endl;
			decompressed.clear();
			return false;
		}

		if (g_debug > 8)
		{
			std::cout << "read " << rsrc.size() << " bytes from resource fork" << std::endl;
			DumpBuffer(rsrc.data(), rsrc.size(), "rsrc content");
		}

		RsrcForkHeader rsrc_hdr;

		memcpy(&rsrc_hdr, rsrc.data(), sizeof(rsrc_hdr));
		/*
		rsrc_hdr.data_off_be = bswap_32(rsrc_hdr.data_off_be);
		rsrc_hdr.data_size_be = bswap_32(rsrc_hdr.data_size_be);
		rsrc_hdr.mgmt_off_be = bswap_32(rsrc_hdr.mgmt_off_be);
		rsrc_hdr.mgmt_size_be = bswap_32(rsrc_hdr.mgmt_size_be);
		*/

		if (g_debug > 8)
		{
			std::cout << "computed values:" << std::endl;
			std::cout << " data offset=" << rsrc_hdr.data_offset << std::endl;
			std::cout << " data size=" << rsrc_hdr.data_size << std::endl;
			std::cout << " mgmt offset=" << rsrc_hdr.mgmt_offset << std::endl;
			std::cout << " mgmt size=" << rsrc_hdr.mgmt_size << std::endl;
		}

		// uint32_t rsrc_size = bswap_32(*reinterpret_cast<uint32_t *>(rsrc.data() + rsrc_hdr.data_off_be));
		if (rsrc_hdr.data_offset > rsrc.size())
		{
			if (g_debug > 0)
				std::cout << "invalid data offset in resource fork header" << std::endl;
			return false;
		}
		const uint8_t *cmpf_rsrc_base = rsrc.data() + rsrc_hdr.data_offset + sizeof(uint32_t);
		const CmpfRsrc *cmpf_rsrc = reinterpret_cast<const CmpfRsrc *>(cmpf_rsrc_base);

		decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

		if (g_debug > 8)
			std::cout << "Decompressed size according to header: " << hdr->size << std::endl;

		// Inflate may write past 0x10000 with incorrect input. This provides
		// a safety margin of sorts.
		uint8_t blk[0x40000];
		size_t k;
		size_t off = 0;

		for (k = 0; k < cmpf_rsrc->entries; k++)
		{
			size_t src_offset = cmpf_rsrc->entry[k].off;
			const uint8_t *src = cmpf_rsrc_base + src_offset;
			size_t src_len = cmpf_rsrc->entry[k].size;
			size_t entry_last_offset = rsrc_hdr.data_offset + src_offset + src_len - 1;
			if (entry_last_offset > rsrc.size())
			{
				if (g_debug > 0)
				{
					std::cout << "Invalid entry (k=" << k << ") in block map: "
					          << "block size extends past end of resource fork" << std::endl;
				}
				return false;
			}

			size_t expected = expected_block_len(k, hdr->size);
			if ((src_len == 0x10001) || ((src[0] & 0x0f) == 0x0f))
			{
				// not compressed
				src++;
				src_len--;
				if (src_len != expected)
				{
					if (g_debug > 0)
						std::cout << "Invalid content in block " << k << ": expected "
						          << expected << " bytes, but uncompressed block has size "
							  << src_len << std::endl;
					if (!g_lax)
						return false;
				}
				memcpy(blk, src, src_len);
			} else if (src_len > 0x10000) {
				if (g_debug > 0) {
					std::cout << "Invalid map entry: offset=" << src_offset
						<< ", size=" << src_len << "." << std::endl;
				}
				return false;
			} else {

				off = DecompressZLib(blk, 0x10000, cmpf_rsrc_base + cmpf_rsrc->entry[k].off, cmpf_rsrc->entry[k].size);

				if (g_debug > 8)
					std::cout << "DecompressZLib dst = " << (0x10000 * k) << " / 10000 src = " << cmpf_rsrc->entry[k].off << " / " << cmpf_rsrc->entry[k].size << " => " << off << std::endl;

				if (off != expected)
				{
					if (g_debug > 0)
						std::cout << "Wrong uncompressed size for block " << k << ": expected "
							  << expected << " bytes, found " << off << std::endl;
					if (!g_lax)
						return false;
				}

				if ((0x10000 * (k + 1) - 1) > decompressed.size())
				{
					if (g_debug > 0)
						std::cout << "More decompressed data than expected!" << std::endl;

					if (!g_lax)
						return false;
				}
			}

			std::copy(blk, blk + 0x10000, decompressed.begin() + (0x10000 * k));
		}
	}
	else if (hdr->algo == 7)
	{
		if (g_debug > 8)
			std::cout << "Decompress LZVN compressed file " << compressed.size() << " => " << hdr->size << std::endl;

		decompressed.resize(hdr->size);

		if (cdata[0] == 0x06)
			decompressed.assign(cdata + 1, cdata + csize);
		else
			DecompressLZVN(decompressed.data(), decompressed.size(), cdata, csize);
	}
	else if (hdr->algo == 8)
	{
		if (g_debug > 8)
			std::cout << "Decompress LZVN compressed resource file" << std::endl;

		std::vector<uint8_t> rsrc;
		size_t k;

		bool rc = dir.GetAttribute(rsrc, ino, "com.apple.ResourceFork");

		if (!rc)
		{
			decompressed.clear();
			return false;
		}

		const uint32_t *off_list = reinterpret_cast<const uint32_t *>(rsrc.data());

		size_t decompressed_new_size = (hdr->size + 0xFFFF) & 0xFFFF0000;

		decompressed.resize(decompressed_new_size);

		if (g_debug > 8)
		{
			std::cout << "rsrc data size = " << rsrc.size() << std::endl;
			std::cout << "hdr claims that size is " << hdr->size << std::endl;
			std::cout << "allocated: " << decompressed_new_size << std::endl;
		}

		/*
		if (g_debug > 8)
			DumpBuffer(rsrc.data(), rsrc.size(), "rsrc content");
		*/

		for (k = 0; (k << 16) < decompressed.size(); k++)
		{
			size_t k_offset = k << 16;
			void *dst = decompressed.data() + k_offset;
			size_t dst_len = 0x10000;
			const uint8_t *src = rsrc.data() + off_list[k];
			size_t src_len = off_list[k+1] - off_list[k];
			size_t expected = expected_block_len(k, hdr->size);
			if (g_debug > 8)
			{
				std::cout << " k=" << k << ": off_list[k]=" << off_list[k] << std::endl;
				std::cout << " size=" << src_len;
				std::cout << std::endl;
				std::cout.flush();
			}
			if ((off_list[k+1] < off_list[k]) || (off_list[k+1] > rsrc.size()))
			{
				if (g_debug > 0)
					std::cout << "invalid offset" << std::endl;

				return false;
			}
			// lzvn_decode(decompressed.data() + (k << 16), 0x10000, rsrc.data() + off_list[k], off_list[k + 1] - off_list[k]);
			// if len == 0x10001 the block is not compressed!
			// also, if src[0] == 0x06...
			if (src_len == 0x10001 || src[0] == 0x06)
			{
				src++;
				src_len--;
				if (src_len != expected)
				{
					if (g_debug > 0)
						std::cout << "Invalid content in block " << k << ": expected "
						          << expected << " bytes, but uncompressed block has size "
							  << src_len << std::endl;
					if (!g_lax)
						return false;
				}
				memcpy(dst, src, src_len);
			}
			else if (src_len > 0x10000)
			{
				if (g_debug > 0)
					std::cout << "Invalid compressed block size";
				if (!g_lax)
					return false;
			}
			else
			{
				size_t decoded_size = DecompressLZVN(reinterpret_cast<uint8_t *>(dst), dst_len, src, src_len);
				if (g_debug > 8)
					std::cout << "lzvn_decode got " << decoded_size << std::endl;

				if (decoded_size != expected)
				{
					if (g_debug > 0)
						std::cout << "Wrong uncompressed size for block " << k << ": expected "
							  << expected << " bytes, found " << decoded_size << std::endl;
					if (!g_lax)
						return false;
				}
			}
		}

		decompressed.resize(hdr->size);

		return rc;
	}
	else
	{
		if (g_debug > 0)
			std::cout << "DecompressFile: Unknown Algorithm " << hdr->algo << std::endl;

		std::cerr << "Unknown algo " << hdr->algo << std::endl;
		std::cerr << "stream size: " << compressed.size() << std::endl;
		DumpBuffer(compressed.data(), compressed.size(), "compressed stream content");

		if (g_lax)
			decompressed = compressed;
		else
			return false;
	}
#else
	decompressed = compressed;
#endif

	return true;
}

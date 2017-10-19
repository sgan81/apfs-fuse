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

#include <fstream>
#include <iostream>
#include <iomanip>

#include <ApfsLib/Util.h>
#include <ApfsLib/DiskStruct.h>
#include <ApfsLib/BlockDumper.h>

#ifdef __linux__
#include <signal.h>
#endif

#undef RAW_VERBOSE

constexpr size_t BLOCKSIZE = 0x1000;

volatile bool g_abort = 0;

void DumpBlockTrunc(std::ostream &os, const byte_t *data)
{
	unsigned int sz = BLOCKSIZE - 1;

	while (sz > 0 && data[sz] == 0)
		sz--;

	sz = (sz + 0x10) & 0xFFFFFFF0;

	DumpHex(os, data, sz);
}

void MapBlocks(std::ostream &os, std::istream &dev)
{
	using namespace std;

	uint64_t size;
	uint64_t blk_nr;
	uint8_t block[BLOCKSIZE];
	const APFS_BlockHeader * const blk = reinterpret_cast<const APFS_BlockHeader *>(block);
	const APFS_TableHeader * const tbl = reinterpret_cast<const APFS_TableHeader *>(block + sizeof(APFS_BlockHeader));
	bool last_was_used = false;

	os << hex << uppercase << setfill('0');

	os << "[Block]  | Node ID  | Version  | Type     | Subtype  | Flgs | Levl | Entries  | Description" << endl;
	os << "---------+----------+----------+----------+----------+------+------+----------+---------------------------------" << endl;

	dev.seekg(0, std::ios::end);
	size = dev.tellg();
	dev.seekg(0);

	size /= BLOCKSIZE;

	for (blk_nr = 0; blk_nr < size && !g_abort; blk_nr++)
	{
		dev.read(reinterpret_cast<char *>(block), BLOCKSIZE);

		if (IsEmptyBlock(block, BLOCKSIZE))
		{
			if (last_was_used)
				os << "---------+----------+----------+----------+----------+------+------+----------+ Empty" << endl;
			last_was_used = false;
			continue;
		}

		if (VerifyBlock(block, BLOCKSIZE))
		{
			os << setw(8) << blk_nr << " | ";
			os << setw(8) << blk->node_id << " | ";
			os << setw(8) << blk->version << " | ";
			os << setw(8) << blk->type << " | ";
			os << setw(8) << blk->subtype << " | ";
			os << setw(4) << tbl->page << " | ";
			os << setw(4) << tbl->level << " | ";
			os << setw(8) << tbl->entries_cnt << " | ";
			os << BlockDumper::GetNodeType(blk->type, blk->subtype);
			if ((blk->type & 0xFFFFFFF) == 2)
				os << " [Root]";
			os << endl;
			last_was_used = true;
		}
		else
		{
			os << setw(8) << blk_nr;
			os << " |          |          |          |          |      |      |          | Data" << endl;
			last_was_used = true;
		}
	}

	os << endl;
}

void ScanBlocks(std::ostream &os, std::istream &dev)
{
	BlockDumper bd(os, BLOCKSIZE);
	uint64_t size;
	uint64_t blk_nr;
	uint8_t block[BLOCKSIZE];

	dev.seekg(0, std::ios::end);
	size = dev.tellg();
	dev.seekg(0);

	size /= BLOCKSIZE;

	bd.SetTextFlags(0x08);

	for (blk_nr = 0; blk_nr < size && !g_abort; blk_nr++)
	{
		dev.read(reinterpret_cast<char *>(block), BLOCKSIZE);

		if (IsEmptyBlock(block, BLOCKSIZE))
			continue;

		if (VerifyBlock(block, BLOCKSIZE))
			bd.DumpNode(block, blk_nr);
		else
		{
#if 0
			os << std::hex << std::setw(16) << blk_nr << std::endl;
			DumpBlockTrunc(os, block);
			os << std::endl;
			os << "========================================================================================================================" << std::endl;
			os << std::endl;
#endif
		}
	}
}

static void ctrl_c_handler(int sig)
{
	g_abort = true;
}

int main(int argc, const char *argv[])
{
	if (argc < 3)
	{
		std::cerr << "Syntax: Apfs file.img output.txt [map.txt]" << std::endl;
		return 1;
	}

	std::ifstream dev;
	std::ofstream os;

#ifdef __linux__
	signal(SIGINT, ctrl_c_handler);
#endif

	dev.open(argv[1], std::ios::binary);

	if (!dev.is_open())
	{
		std::cerr << "File " << argv[1] << " not found." << std::endl;
		return 2;
	}

	if (argc > 3)
	{
		os.open(argv[3]);
		if (!os.is_open())
		{
			std::cerr << "Could not open output file " << argv[3] << std::endl;
			return 3;
		}

		MapBlocks(os, dev);
		os.close();
	}

	os.open(argv[2]);
	if (!os.is_open())
	{
		std::cerr << "Could not open output file " << argv[2] << std::endl;
		return 3;
	}

	ScanBlocks(os, dev);

	dev.close();
	os.close();

	return 0;
}

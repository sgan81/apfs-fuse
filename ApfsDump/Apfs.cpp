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

#include <memory>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>

#include <ApfsLib/Device.h>
#include <ApfsLib/Util.h>
#include <ApfsLib/DiskStruct.h>
#include <ApfsLib/BlockDumper.h>
#include <ApfsLib/GptPartitionMap.h>

#include "Dumper.h"

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

#if 0

void MapBlocks(std::ostream &os, Device &dev, uint64_t bid_start, uint64_t bcnt)
{
	using namespace std;

	uint64_t bid;
	uint8_t block[BLOCKSIZE];
	const APFS_ObjHeader * const blk = reinterpret_cast<const APFS_ObjHeader *>(block);
	const APFS_TableHeader * const tbl = reinterpret_cast<const APFS_TableHeader *>(block + sizeof(APFS_ObjHeader));
	bool last_was_used = false;

	os << hex << uppercase << setfill('0');

	os << "[Block]  | oid      | xid      | type     | subtype  | Page | Levl | Entries  | Description" << endl;
	os << "---------+----------+----------+----------+----------+------+------+----------+---------------------------------" << endl;

	for (bid = 0; bid < bcnt && !g_abort; bid++)
	{
		if ((bid & 0xFFF) == 0)
		{
			std::cout << '.';
			std::cout.flush();
		}

		dev.Read(block, (bid_start + bid) * BLOCKSIZE, BLOCKSIZE);

		if (IsEmptyBlock(block, BLOCKSIZE))
		{
			if (last_was_used)
				os << "---------+----------+----------+----------+----------+------+------+----------+ Empty" << endl;
			last_was_used = false;
			continue;
		}

		if (VerifyBlock(block, BLOCKSIZE))
		{
			os << setw(8) << bid << " | ";
			os << setw(8) << blk->oid << " | ";
			os << setw(8) << blk->xid << " | ";
			os << setw(8) << blk->type << " | ";
			os << setw(8) << blk->subtype << " | ";
			os << setw(4) << tbl->page << " | ";
			os << setw(4) << tbl->level << " | ";
			os << setw(8) << tbl->entries_cnt << " | ";
			os << BlockDumper::GetNodeType(blk->type, blk->subtype);
			if (APFS_OBJ_TYPE(blk->type) == 2)
				os << " [Root]";
			os << endl;
			last_was_used = true;
		}
		else
		{
			os << setw(8) << bid;
			os << " |          |          |          |          |      |      |          | Data" << endl;
			last_was_used = true;
		}
	}

	os << endl;
}

void ScanBlocks(std::ostream &os, Device &dev, uint64_t bid_start, uint64_t bcnt)
{
	BlockDumper bd(os, BLOCKSIZE);
	uint64_t bid;
	uint8_t block[BLOCKSIZE];

	bd.SetTextFlags(0x00);

	for (bid = 0; bid < bcnt && !g_abort; bid++)
	{
		if ((bid & 0xFFF) == 0)
		{
			std::cout << '.';
			std::cout.flush();
		}

		dev.Read(block, (bid_start + bid) * BLOCKSIZE, BLOCKSIZE);

		if (IsEmptyBlock(block, BLOCKSIZE))
			continue;

		if (VerifyBlock(block, BLOCKSIZE))
			bd.DumpNode(block, bid);
		else
		{
#if 0
			os << std::hex << std::setw(16) << blk_nr << std::endl;
			DumpBlockTrunc(os, block);
			os << std::endl;
			os << "===========================================================================================================================" << std::endl;
			os << std::endl;
#endif
		}
	}
}

void DumpSpaceman(std::ostream &os, Device &dev, uint64_t bid_start, uint64_t bcnt)
{
	uint8_t sb[BLOCKSIZE];
	uint8_t data[BLOCKSIZE];
	BlockDumper bd(os, BLOCKSIZE);
	const APFS_ObjHeader *bhdr;
	const APFS_Spaceman *sm;
	const APFS_NX_Superblock *nxsb;
	uint64_t bid;
	uint64_t cnt;
	uint64_t smbmp_bid = 0;
	uint64_t smbmp_cnt = 0;
	uint64_t volbmp_bid = 0;
	uint64_t volbmp_cnt = 0;

	(void)bcnt;

	if (!dev.Read(sb, bid_start * BLOCKSIZE, BLOCKSIZE))
		return;

	if (!VerifyBlock(sb, BLOCKSIZE))
		return;

	nxsb = reinterpret_cast<const APFS_NX_Superblock *>(sb);
	if (nxsb->hdr.type != 0x80000001)
		return;

	if (nxsb->nx_magic != 0x4253584E)
		return;

	bd.DumpNode(sb, 0);

	os << std::endl;
	os << "Dumping Superblock Area" << std::endl;
	os << std::endl;

	bid = nxsb->nx_xp_desc_base;
	cnt = nxsb->nx_xp_desc_blocks;

	while (cnt > 0)
	{
		dev.Read(data, (bid_start + bid) * BLOCKSIZE, BLOCKSIZE);
		bd.DumpNode(data, bid);
		cnt--;
		bid++;
	}

	os << std::endl;
	os << "Dumping Spaceman Area" << std::endl;
	os << std::endl;

	bid = nxsb->nx_xp_data_base;
	cnt = nxsb->nx_xp_data_blocks;

	while (cnt > 0)
	{
		dev.Read(data, (bid_start + bid) * BLOCKSIZE, BLOCKSIZE);
		bd.DumpNode(data, bid);
		cnt--;
		bid++;

		bhdr = reinterpret_cast<const APFS_ObjHeader *>(data);
		if (bhdr->type == 0x80000005)
		{
			sm = reinterpret_cast<const APFS_Spaceman *>(data);
			smbmp_bid = sm->ip_bm_base_address;
			smbmp_cnt = sm->ip_bitmap_block_count;
			volbmp_bid = sm->ip_base_address;
			volbmp_cnt = sm->ip_block_count;
		}
	}

	bid = smbmp_bid;
	cnt = smbmp_cnt;

	while (cnt > 0)
	{
		dev.Read(data, (bid_start + bid) * BLOCKSIZE, BLOCKSIZE);
		bd.DumpNode(data, bid);
		cnt--;
		bid++;
	}

	bid = volbmp_bid;
	cnt = volbmp_cnt;

	while (cnt > 0)
	{
		dev.Read(data, (bid_start + bid) * BLOCKSIZE, BLOCKSIZE);
		bd.DumpNode(data, bid);
		cnt--;
		bid++;
	}
}

#endif

static void ctrl_c_handler(int sig)
{
	(void)sig;
	g_abort = true;
}

#if 0

int main(int argc, const char *argv[])
{
	if (argc < 3)
	{
		std::cerr << "Syntax: apfs-dump file.img output.txt [map.txt]" << std::endl;
		return 1;
	}

	Device *dev;
	std::ofstream os;
	uint64_t bid_start = 0;
	uint64_t bcnt = 0;

	g_debug = 255;

#if defined(__linux__) || defined(__APPLE__)
	signal(SIGINT, ctrl_c_handler);
#endif

	dev = Device::OpenDevice(argv[1]);

	if (!dev)
	{
		std::cerr << "Device " << argv[1] << " not found." << std::endl;
		return 2;
	}

	{
		GptPartitionMap pmap;
		if (pmap.LoadAndVerify(*dev))
		{
			int partid = pmap.FindFirstAPFSPartition();
			if (partid >= 0)
			{
				std::cout << "Dumping EFI partition" << std::endl;
				pmap.GetPartitionOffsetAndSize(partid, bid_start, bcnt);
				bid_start /= BLOCKSIZE;
				bcnt /= BLOCKSIZE;
			}
		}
	}

	if (bcnt == 0)
		bcnt = dev->GetSize() / BLOCKSIZE;

	if (argc > 3)
	{
		os.open(argv[3]);
		if (!os.is_open())
		{
			std::cerr << "Could not open output file " << argv[3] << std::endl;
			dev->Close();
			delete dev;
			return 3;
		}

		MapBlocks(os, *dev, bid_start, bcnt);
		os.close();
	}

	os.open(argv[2]);
	if (!os.is_open())
	{
		std::cerr << "Could not open output file " << argv[2] << std::endl;
		dev->Close();
		delete dev;
		return 3;
	}

	DumpSpaceman(os, *dev, bid_start, bcnt);
	ScanBlocks(os, *dev, bid_start, bcnt);

	dev->Close();
	os.close();

	delete dev;

	return 0;
}

#else

void usage()
{
	std::cerr << "Syntax:" << std::endl;
	std::cerr << "apfs-dump [-map mapfile.txt] file.img output.txt" << std::endl;
	std::cerr << "apfs-dump [-map mapfile.txt] -fusion main.img tier2.img output.txt" << std::endl;
}

int main(int argc, const char *argv[])
{
	const char *name_dev_main = 0;
	const char *name_dev_tier2 = 0;
	const char *name_map = 0;
	const char *name_output = 0;
	bool use_fusion = false;

	int n;
	int idx = 0;

	for (n = 1; n < argc; n++)
	{
		if (!strcmp(argv[n], "-map"))
		{
			n++;
			if (n >= argc)
			{
				usage();
				return 1;
			}
			name_map = argv[n];
		}

		else if (!strcmp(argv[n], "-fusion"))
			use_fusion = true;

		else
		{
			switch (idx)
			{
				case 0:
					name_dev_main = argv[n];
					printf("main: %s\n", name_dev_main);
					if (use_fusion)
						idx++;
					else
						idx += 2;
					break;
				case 1:
					name_dev_tier2 = argv[n];
					printf("tier2: %s\n", name_dev_tier2);
					idx++;
					break;
				case 2:
					name_output = argv[n];
					printf("out: %s\n", name_output);
					idx++;
					break;
				default:
					usage();
					return 1;
					break;
			}
		}
	}

	printf("main: %s\n", name_dev_main);

	if ((name_output == 0) || (use_fusion && name_dev_tier2 == 0))
	{
		usage();
		return 1;
	}

	std::unique_ptr<Device> dev_main;
	std::unique_ptr<Device> dev_tier2;
	std::ofstream os;

	g_debug = 255;

#if defined(__linux__) || defined(__APPLE__)
	signal(SIGINT, ctrl_c_handler);
#endif

	dev_main.reset(Device::OpenDevice(name_dev_main));
	if (use_fusion)
		dev_tier2.reset(Device::OpenDevice(name_dev_tier2));

	if (!dev_main || !dev_tier2)
	{
		if (!dev_main)
			std::cerr << "Device " << name_dev_main << " not found." << std::endl;

		if (!dev_tier2)
			std::cerr << "Device " << name_dev_tier2 << " not found." << std::endl;

		return 2;
	}

	{
		Dumper dmp(dev_main.get(), dev_tier2.get());

		if (!dmp.Initialize())
			return -1;

#if 1
		if (name_map)
		{
			os.open(name_map);
			if (!os.is_open())
			{
				std::cerr << "Could not open map file " << name_map << std::endl;
				dev_main->Close();
				if (dev_tier2)
					dev_tier2->Close();
				return 3;
			}

			dmp.DumpBlockList(os);
			os.close();
		}
#endif

		os.open(name_output);
		if (!os.is_open())
		{
			std::cerr << "Could not open output file " << name_output << std::endl;
			dev_main->Close();
			if (dev_tier2)
				dev_tier2->Close();
			return 3;
		}

		dmp.DumpContainer(os);
	}

	dev_main->Close();
	if (dev_tier2)
		dev_tier2->Close();
	os.close();

	return 0;
}

#endif

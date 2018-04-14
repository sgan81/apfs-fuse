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

#include <iostream>
#include <fstream>

#include <ApfsLib/Device.h>
#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/ApfsVolume.h>
#include <ApfsLib/BlockDumper.h>
#include <ApfsLib/ApfsDir.h>
#include <ApfsLib/GptPartitionMap.h>

int main(int argc, char *argv[])
{
	bool rc;
	int volumes_cnt;
	int volume_id;
	std::unique_ptr<Device> disk;
	uint64_t offset;
	uint64_t size;

	if (argc < 3)
	{
		std::cerr << "Syntax: apfs-dump-quick <filename.dmg> <Logfile.txt>" << std::endl;
		return -1;
	}

	g_debug = 16;

	disk.reset(Device::OpenDevice(argv[1]));

	if (!disk)
	{
		std::cerr << "Unable to open file " << argv[1] << std::endl;
		return -1;
	}

	std::ofstream st;
	st.open(argv[2]);

	if (!st.is_open())
	{
		std::cerr << "Unable to open output file " << argv[2] << std::endl;
		disk->Close();
		return -1;
	}

	offset = 0;
	size = disk->GetSize();

	GptPartitionMap gpt;
	if (gpt.LoadAndVerify(*disk.get()))
	{
		int n = gpt.FindFirstAPFSPartition();
		if (n != -1)
			gpt.GetPartitionOffsetAndSize(n, offset, size);
	}

	ApfsContainer *container = new ApfsContainer(*disk.get(), offset, size);

	rc = container->Init();

	if (!rc)
	{
		std::cerr << "Unable to init container." << std::endl;
		delete container;
		disk->Close();
		return -1;
	}

#if 1
	BlockDumper bd(st, container->GetBlocksize());

	container->dump(bd);

	volumes_cnt = container->GetVolumeCnt();

#if 1
	for (volume_id = 0; volume_id < volumes_cnt; volume_id++)
	{
		ApfsVolume *vol;

		vol = container->GetVolume(volume_id);

		if (vol)
		{
			std::cout << "Volume " << volume_id << ": " << vol->name() << std::endl;

			vol->dump(bd);
		}

		delete vol;
	}
#endif
#endif

#if 0
	ApfsVolume *vol;

	vol = container->GetVolume(0);

	if (vol)
	{
		ApfsDir dir(*vol);
		ApfsDir::Inode ino;
		std::vector<ApfsDir::Name> root_list;

#if 0
		dir.GetInode(ino, 0x15);

		std::cout << "Directory is called " << ino.name << std::endl;

		dir.ListDirectory(root_list, 0x15);

		for (auto it = root_list.cbegin(); it != root_list.cend(); ++it)
		{
			std::cout << it->name << " : " << it->inode_id << std::endl;
		}
#endif

#if 1
		std::vector<uint8_t> buf;
		buf.resize(0x29000);

		dir.ReadFile(buf.data(), 0x39, 0, 0x29000);
#endif
	}

#endif

	delete container;

	st.close();

	disk->Close();

	return 0;
}

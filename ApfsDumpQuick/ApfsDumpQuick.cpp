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
#include <cstring>
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
	int n;
	std::unique_ptr<Device> main_disk;
	std::unique_ptr<Device> tier2_disk;
	uint64_t main_offset;
	uint64_t tier2_offset;
	uint64_t main_size;
	uint64_t tier2_size;

	const char *main_name;
	const char *tier2_name;
	const char *output_name;

	if (argc < 3)
	{
		std::cerr << "Syntax: apfs-dump-quick <main-device> [-f fusion-device] <Logfile.txt>" << std::endl;
		return -1;
	}

	main_name = argv[1];
	if (!strcmp(argv[2], "-f"))
	{
		if (argc < 5)
			std::cerr << "Syntax: apfs-dump-quick <main-device> [-f fusion-secondary-device] <Logfile.txt>" << std::endl;

		tier2_name = argv[3];
		output_name = argv[4];
	}
	else
	{
		tier2_name = nullptr;
		output_name = argv[2];
	}

	g_debug = 255;

	main_disk.reset(Device::OpenDevice(main_name));
	if (tier2_name)
		tier2_disk.reset(Device::OpenDevice(tier2_name));

	if (!main_disk)
	{
		std::cerr << "Unable to open device " << main_name << std::endl;
		return -1;
	}

	if (tier2_name && !tier2_disk)
	{
		std::cerr << "Unable to open secondary device " << tier2_name << std::endl;
		return -1;
	}

	std::ofstream st;
	st.open(output_name);

	if (!st.is_open())
	{
		std::cerr << "Unable to open output file " << output_name << std::endl;
		      main_disk->Close();
		return -1;
	}

	main_offset = 0;
	main_size = main_disk->GetSize();

	tier2_offset = 0;
	tier2_size = tier2_disk->GetSize();

	GptPartitionMap gpt;
	if (gpt.LoadAndVerify(*main_disk.get()))
	{
		std::cout << "Info: Found valid GPT partition table on main device. Dumping first APFS partition." << std::endl;

		n = gpt.FindFirstAPFSPartition();
		if (n != -1)
			gpt.GetPartitionOffsetAndSize(n, main_offset, main_size);
	}

	if (tier2_disk && gpt.LoadAndVerify(*tier2_disk.get()))
	{
		std::cout << "Info: Found valid GPT partition table on tier2 device. Dumping first APFS partition." << std::endl;

		n = gpt.FindFirstAPFSPartition();
		if (n != -1)
			gpt.GetPartitionOffsetAndSize(n, tier2_offset, tier2_size);
	}

	std::unique_ptr<ApfsContainer> container(new ApfsContainer(main_disk.get(), main_offset, main_size, tier2_disk.get(), tier2_offset, tier2_size));

	rc = container->Init();

	if (!rc)
	{
		std::cerr << "Unable to init container." << std::endl;
		container.reset();
		main_disk->Close();
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

	container.reset();

	st.close();

	main_disk->Close();

	return 0;
}

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

#ifdef __APPLE__

#include <unistd.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <iostream>

#include "DeviceMac.h"
#include "Global.h"

DeviceMac::DeviceMac()
{
	m_device = -1;
	m_size = 0;
}

DeviceMac::~DeviceMac()
{
	Close();
}

bool DeviceMac::Open(const char* name)
{
	m_device = open(name, O_RDONLY);

	if (m_device == -1)
	{
		printf("Opening device %s failed. %s\n", name, strerror(errno));
		return false;
	}

	struct stat st;

	fstat(m_device, &st);

	std::cout << "st_mode = " << st.st_mode << std::endl;

	if (S_ISREG(st.st_mode))
	{
		m_size = st.st_size;
	}
	else if (S_ISBLK(st.st_mode))
	{
		uint64_t sector_count = 0;
		uint32_t sector_size = 0;

		ioctl(m_device, DKIOCGETBLOCKCOUNT, &sector_count);
		ioctl(m_device, DKIOCGETBLOCKSIZE, &sector_size);

		m_size = sector_size * sector_count;

		std::cout << "Sector count = " << sector_count << std::endl;
		std::cout << "Sector size  = " << sector_size << std::endl;
	}
	else
	{
		std::cout << "File mode unknown!" << std::endl;
	}

	if (g_debug > 0)
		printf("Device %s opened. Size is %zu bytes.\n", name, m_size);

	return m_device != -1;
}

void DeviceMac::Close()
{
	if (m_device != -1)
		close(m_device);
	m_device = -1;
	m_size = 0;
}

bool DeviceMac::Read(void* data, uint64_t offs, uint64_t len)
{
	size_t nread;

	nread = pread(m_device, data, len, offs);

	// TODO: Better error handling ...
	return nread == len;
}

#endif

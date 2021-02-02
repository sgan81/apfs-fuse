/*
This file is part of apfs-fuse, a read-only implementation of APFS
(Apple File System) for FUSE.
Copyright (C) 2017 Simon Gander
Copyright (C) 2021 John Othwolo

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

#if defined (__FreeBSD__) // check other BSDs

#include <unistd.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <iostream>

#include "DeviceBSD.h"
#include "Global.h"

DeviceBSD::DeviceBSD()
{
	m_device = -1;
	m_size = 0;
}

DeviceBSD::~DeviceBSD()
{
	Close();
}

bool DeviceBSD::Open(const char* name)
{
	m_device = open(name, O_RDONLY);

	if (m_device == -1)
	{
		std::cout << "Opening device " << name << " failed with error " << strerror(errno) << std::endl;
		return false;
	}

	struct stat st;

	fstat(m_device, &st);

	std::cout << "st_mode = " << st.st_mode << std::endl;

	if (S_ISREG(st.st_mode))
	{
		m_size = st.st_size;
	}
	else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode))
	{
		// uint64_t sector_count = 0;
		uint32_t sector_size = 0;
        	uint32_t media_size = 0;

        	ioctl(m_device, DIOCGMEDIASIZE, &media_size);
		// ioctl(m_device, DKIOCGETBLOCKCOUNT, &sector_count);
		ioctl(m_device, DIOCGSECTORSIZE, &sector_size);

		// m_size = sector_size * sector_count;
        	m_size = media_size;
	
	        if ((media_size%sector_size) != 0)
        	{
	            std::cerr << "Something is really wrong!!";
	        }
		// std::cout << "Sector count = " << sector_count << std::endl;
		std::cout << "Sector size  = " << sector_size << std::endl;
	        std::cout << "Media size  = " << media_size << std::endl;
	}
	else
	{
		std::cout << "File mode unknown!" << std::endl;
	}

	if (g_debug & Dbg_Info)
		std::cout << "Device " << name << " opened. Size is " << m_size << std::endl;

	return m_device != -1;
}

void DeviceBSD::Close()
{
	if (m_device != -1)
		close(m_device);
	m_device = -1;
	m_size = 0;
}

bool DeviceBSD::Read(void* data, uint64_t offs, uint64_t len)
{
    size_t nread;

    nread = pread(m_device, data, len, offs);

    // TODO: Better error handling ...
    return nread == len;
}

bool DeviceBSD::Write(void* data, uint64_t offs, uint64_t len)
{
    // TODO: Better error handling ...
    return len == static_cast<uint64_t >(pwrite(m_device, data, len, offs));
}

#endif


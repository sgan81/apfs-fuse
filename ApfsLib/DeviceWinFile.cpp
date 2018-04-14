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

#ifdef _WIN32

#include "DeviceWinFile.h"

DeviceWinFile::DeviceWinFile()
{
	m_size = 0;
}

DeviceWinFile::~DeviceWinFile()
{
	Close();
}

bool DeviceWinFile::Open(const char * name)
{
	m_vol.open(name, std::ios::binary);

	if (!m_vol.is_open())
		return false;

	m_vol.seekg(0, std::ios::end);
	m_size = m_vol.tellg();
	m_vol.seekg(0);

	return m_vol.is_open();
}

void DeviceWinFile::Close()
{
	m_vol.close();
}

bool DeviceWinFile::Read(void *data, uint64_t offs, uint64_t len)
{
	m_vol.seekg(offs);
	m_vol.read(reinterpret_cast<char *>(data), len);

	// TODO: Fehlerbehandlung ...
	return true;
}

#endif

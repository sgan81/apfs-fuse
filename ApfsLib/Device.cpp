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

#include <cstring>

#include "Device.h"

#include "DeviceWinFile.h"
#include "DeviceWinPhys.h"
#include "DeviceLinux.h"
#include "DeviceMac.h"
#include "DeviceDMG.h"
#include "DeviceSparsebundle.h"
#include "DeviceDMG.h"

#undef DISABLE_DMG

Device::Device()
{
}

Device::~Device()
{
}

Device * Device::OpenDevice(const char * name)
{
	Device *dev = nullptr;
#ifndef DISABLE_DMG
	DeviceImageDisk *dmg = nullptr;
#endif
	bool rc;

#ifdef _WIN32
	if (!strncmp(name, "\\\\.\\PhysicalDrive", 17))
	{
		dev = new DeviceWinPhys();
		rc = dev->Open(name);
		if (!rc)
		{
			dev->Close();
			delete dev;
			dev = nullptr;
		}
	}
#ifndef DISABLE_DMG
	else
		dmg = new DeviceDMG();
#endif
#else
#ifndef DISABLE_DMG
	if (strncmp(name, "/dev/", 5)) {
		if (strstr(name, ".sparsebundle") != name+strlen(name)-strlen(".sparsebundle"))
			dmg = new DeviceDMG();
	}
#endif
#ifndef DISABLE_SPARSEBUNDLE
	if (strncmp(name, "/dev/", 5) != 0) {
		if (strstr(name, ".sparsebundle") == name+strlen(name)-strlen(".sparsebundle"))
			dmg = new DeviceSparsebundle();
	}
#endif
#endif
#ifndef DISABLE_DMG
	if (dmg)
	{
		rc = dmg->Open(name);
		if (rc)
		{
			dev = dmg;
		}
		else
		{
			dmg->Close();
			delete dmg;
			dmg = nullptr;
		}
	}
#endif

	if (!dev)
	{
#ifdef _WIN32
		dev = new DeviceWinFile();
#endif
#ifdef __linux__
		dev = new DeviceLinux();
#endif
#ifdef __APPLE__
		dev = new DeviceMac();
#endif
		rc = dev->Open(name);

		if (!rc)
		{
			dev->Close();
			delete dev;
			dev = nullptr;
		}
	}

	return dev;
}

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

#pragma once

#ifdef __linux__

#include <cstddef>
#include "Device.h"

class DeviceLinux : public Device
{
public:
	DeviceLinux();
	~DeviceLinux();

	bool Open(const char *name) override;
	void Close() override;

	bool Read(void *data, uint64_t offs, uint64_t len) override;

	uint64_t GetSize() const override { return m_size; }

private:
	int m_device;
	size_t m_size;
};

#endif

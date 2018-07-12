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

#include <cstdint>
#include <exception>

#include <stdio.h> // for asprintf
#include <stdarg.h> // for va_list
#include <stdlib.h> // for free

class DeviceException : public std::exception
{
public:
	DeviceException(const char* format, ...)
	{
		va_list ap;
		va_start(ap, format);
		vasprintf(&m_reason, format, ap);
		va_end(ap);
	}
	virtual ~DeviceException() { free(m_reason); }

	const char *what() const noexcept override { return m_reason; }

private:
	char *m_reason;
};

class Device
{
protected:
	Device();

public:
	virtual ~Device();

	virtual bool Open(const char *name) = 0;
	virtual void Close() = 0;

	virtual bool Read(void *data, uint64_t offs, uint64_t len) = 0;
	virtual uint64_t GetSize() const = 0;

	static Device *OpenDevice(const char *name);
};

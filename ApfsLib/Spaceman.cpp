/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2023 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Spaceman.h"

Spaceman::Spaceman()
{
	sm_phys = nullptr;
}

Spaceman::~Spaceman()
{
}

int Spaceman::init(const void* params)
{
	(void)params;
	sm_phys = reinterpret_cast<const spaceman_phys_t*>(data());
	return 0;
}

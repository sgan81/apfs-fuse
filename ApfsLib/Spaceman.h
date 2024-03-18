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

#pragma once

#include "Object.h"

class BlockDumper;

int CompareFreeQueueKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);

class Spaceman : public Object
{
public:
	Spaceman();
	~Spaceman();

	int init(const void * params) override;

	uint64_t getFreeBlocks() const { return sm_phys->sm_dev[SD_MAIN].sm_free_count + sm_phys->sm_dev[SD_TIER2].sm_free_count; }

	void dump(BlockDumper& d);

private:
	const spaceman_phys_t* sm_phys;
};

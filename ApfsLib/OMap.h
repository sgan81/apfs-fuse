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
#include "ObjPtr.h"
#include "BTree.h"

int CompareOMapKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);

class OMap : public Object
{
public:
	OMap();
	virtual ~OMap();

	int init(const void * params) override;

	int lookup(oid_t oid, xid_t xid, xid_t* xid_o, uint32_t* flags, uint32_t* size, paddr_t* paddr);

	void dump_tree();
	BTree& tree() { return m_tree; }
private:
	const omap_phys_t* om_phys;
	BTree m_tree;
};

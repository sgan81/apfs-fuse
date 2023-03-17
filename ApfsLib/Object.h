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

#include <cstdint>

#include "ApfsTypes.h"
#include "DiskStruct.h"

struct obj_phys_t;

class Object;
class ObjCache;
class Volume;
class Container;

struct ObjListEntry
{
	ObjListEntry() { next = nullptr; prev = &next; }

	Object* next;
	Object** prev;
};

class Object
{
	friend class ObjCache;
protected:
	Object();
	virtual ~Object();

public:
	virtual int init(const void* params) = 0;

	int retain();
	int release();

	oid_t oid() const { return m_oid; }
	xid_t xid() const { return m_xid; }
	uint32_t type() const { return m_type & OBJECT_TYPE_MASK; }
	uint32_t type_and_flags() const { return m_type; }
	uint32_t subtype() const { return m_subtype; }
	paddr_t paddr() const { return m_paddr; }
	const uint8_t* data() const { return m_data; }
	uint32_t size() const { return m_size; }
	int setData(uint8_t* data, uint32_t size);

	Container* nx();
	Volume* fs() { return m_fs; }
	ObjCache& oc() { return *m_oc; }

private:
	ObjCache* m_oc;
	Volume* m_fs;

	int m_refcnt;

	uint8_t* m_data;
	uint32_t m_size;
	oid_t m_oid;
	xid_t m_xid;
	uint32_t m_type;
	uint32_t m_subtype;
	paddr_t m_paddr;

	ObjListEntry m_le_ht;
	ObjListEntry m_le_lru;
};

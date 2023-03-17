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
#include "ObjPtr.h"

class Object;
class Container;
class Volume;

struct ObjListHead
{
	ObjListHead() {
		first = nullptr;
		last = &first;
	}

	Object* first;
	Object** last;
};

class ObjCache
{
public:
	ObjCache();
	~ObjCache();

	void setContainer(Container* nx, paddr_t nxsb_paddr);
	int getObj(Object*& obj, const void* params, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, Volume* fs = nullptr);

	template<typename T>
	int getObj(ObjPtr<T>& ptr, const void* params, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, Volume* fs = nullptr)
	{
		int err;
		Object* obj = nullptr;

		err = getObj(obj, params, oid, xid, type, subtype, size, paddr, fs);
		if (err) return err;
		ptr = obj;
		return 0;
	}

	Container* nx() { return m_nx; }

private:
	Object* createObjInstance(uint32_t type);
	int readObj(Object& o, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, Volume* fs);

	void htAdd(Object* obj, uint64_t key);
	void htRemove(Object* obj);
	void lruAdd(Object* obj);
	void lruRemove(Object* obj);
	void lruShrink();
	void ephemeralAdd(Object* obj);
	void ephemeralRemove(Object* obj);

	Object** m_hashtable;
	ObjListHead m_lru_list;
	ObjListHead m_ephemeral_list;
	uint64_t m_ht_size;
	uint64_t m_ht_mask;
	uint32_t m_lru_cnt;
	uint32_t m_lru_limit;
	Container* m_nx;
};

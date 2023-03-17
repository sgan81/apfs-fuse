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

#include <cstdlib>
#include <cstring>

#include "ObjCache.h"
#include "Object.h"

Object::Object()
{
	m_oc = nullptr;
	m_fs = nullptr;
	m_refcnt = 0;
	m_data = nullptr;
	m_size = 0;
	m_oid = 0;
	m_xid = 0;
	m_type = 0;
	m_subtype = 0;
	m_paddr = 0;
}

Object::~Object()
{
	if (m_data)
		delete[] m_data;
}

int Object::retain()
{
	m_refcnt++;
	return m_refcnt;
}

int Object::release()
{
	m_refcnt--;
	return m_refcnt;
}

int Object::setData(uint8_t* data, uint32_t size)
{
	if (m_data) delete[] m_data;
	m_data = new uint8_t[size];
	m_size = size;
	memcpy(m_data, data, size);
	const obj_phys_t* o = reinterpret_cast<const obj_phys_t*>(m_data);
	m_oid = o->o_oid;
	m_xid = o->o_xid;
	m_type = o->o_type;
	m_subtype = o->o_subtype;
	m_paddr = o->o_oid;
	init(nullptr);

	return 0;
}

Container * Object::nx()
{
	return m_oc->nx();
}

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

#include <cerrno>
#include <cstdlib>
#include <cinttypes>

#include "DiskStruct.h"
#include "Object.h"
#include "ObjCache.h"
#include "Container.h"
#include "Volume.h"
#include "OMap.h"
#include "Util.h"
#include "BTree.h"
#include "Spaceman.h"

ObjCache::ObjCache()
{
	m_ht_size = 0x400;
	m_ht_mask = 0x3FF;
	m_hashtable = new Object*[m_ht_size];
	std::fill_n(m_hashtable, m_ht_size, nullptr);
	m_lru_cnt = 0;
	m_lru_limit = 4096;
}

ObjCache::~ObjCache()
{
	Object* obj;
	Object* next;

	obj = m_lru_list.first;
	while (obj != nullptr) {
		next = obj->m_le_lru.next;
		htRemove(obj);
#if 0
		if (obj->m_refcnt != 0)
			log_warn("Object %" PRIx64 " still in use!\n", obj->oid());
#endif
		delete obj;
		obj = next;
	}

	obj = m_ephemeral_list.first;
	while (obj != nullptr) {
		next = obj->m_le_lru.next;
		htRemove(obj);
#if 0
		if (obj->m_refcnt != 0)
			log_warn("Object %" PRIx64 " still in use!\n", obj->oid());
#endif
		delete obj;
		obj = next;
	}

	delete[] m_hashtable;
}

void ObjCache::setContainer(Container* nx, paddr_t nxsb_paddr)
{
	m_nx = nx;
	htAdd(nx, 1);
	m_nx->m_oc = this;
	m_nx->m_paddr = nxsb_paddr;
}

int ObjCache::getObj(Object*& obj, const void* params, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, Volume* fs)
{
	Object* o;
	int err;

	obj = nullptr;

	if (xid != 0)
		log_warn("getObj oid %" PRIx64 " xid %" PRIx64 " xid != 0!\n", oid, xid);

	for (o = m_hashtable[oid & m_ht_mask]; o != nullptr; o = o->m_le_ht.next) {
		if (oid == o->m_oid && type == o->m_type && fs == o->fs()) {
			obj = o;
			o->retain();
			if (!(type & OBJ_EPHEMERAL)) {
				lruRemove(o);
				lruAdd(o);
			}
			return 0;
		}
	}

	o = createObjInstance(type);
	if (o == nullptr) return EINVAL;

	if (size == 0)
		size = m_nx->GetBlocksize();

	err = readObj(*o, oid, xid, type, subtype, size, paddr, fs);
	if (err == 0)
		err = o->init(params);
	if (err) {
		delete o;
		return err;
	}

	obj = o;
	htAdd(o, oid);
	if (o->type_and_flags() & OBJ_EPHEMERAL)
		ephemeralAdd(o);
	else {
		lruAdd(o);
		lruShrink();
	}

	return 0;
}

Object * ObjCache::createObjInstance(uint32_t type)
{
	Object* o = nullptr;
	switch (type & OBJECT_TYPE_MASK) {
	case OBJECT_TYPE_NX_SUPERBLOCK:
		// TODO
		break;
	case OBJECT_TYPE_BTREE:
	case OBJECT_TYPE_BTREE_NODE:
		o = new BTreeNode();
		break;
	case OBJECT_TYPE_SPACEMAN:
		o = new Spaceman();
		break;
	case OBJECT_TYPE_OMAP:
		o = new OMap();
		break;
	case OBJECT_TYPE_FS:
		o = new Volume(*m_nx); // TODO Trickery, but could work out ... hmmm ...
		break;
	}
	return o;
}

int ObjCache::readObj(Object& o, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, Volume* fs)
{
	uint32_t o_flags = type & OBJECT_TYPE_FLAGS_MASK;
	int err;
	uint64_t tweak = 0;

	if (paddr == 0) {
		if (o_flags & OBJ_EPHEMERAL) { // TODO ... epehemerals list probably ... yes, but paddr needs to be set.
			log_error("read obj: trying to load ephemeral without valid paddr.\n");
			return EINVAL;
		}
		else if (o_flags & OBJ_PHYSICAL)
			paddr = oid;
		else {
			ObjPtr<OMap> omap;
			uint32_t om_flags;
			uint32_t om_size;
			paddr_t om_paddr;

			if (fs)
				err = fs->getOMap(omap);
			else
				err = m_nx->getOMap(omap);
			if (err) return err;
			err = omap->lookup(oid, xid, nullptr, &om_flags, &om_size, &om_paddr);
			if (err) {
				log_error("obj read: error %d in omap lookup %" PRIx64 " / %" PRIx64 "\n", err, oid, xid);
				return err;
			}
			size = om_size;
			paddr = om_paddr;
			if (om_flags & OMAP_VAL_NOHEADER) {
				o_flags |= OBJ_NOHEADER;
				type |= OBJ_NOHEADER;
			}
			if (om_flags & OMAP_VAL_ENCRYPTED) {
				o_flags |= OBJ_ENCRYPTED;
				type |= OBJ_ENCRYPTED;
				tweak = paddr;
			}
		}
	}

	if (paddr == 0)
		return ENOENT;

	o.m_size = size;
	o.m_data = new uint8_t[size];

	if (fs)
		err = fs->ReadBlocks(o.m_data, paddr, size / m_nx->GetBlocksize(), tweak);
	else
		err = m_nx->ReadBlocks(o.m_data, paddr, size / m_nx->GetBlocksize());
	if (err) return err;

	if (!(o_flags & OBJ_NOHEADER)) {
		if (!VerifyBlock(o.m_data, o.m_size))
			return EINVAL;
	}

	const obj_phys_t* p = reinterpret_cast<const obj_phys_t*>(o.m_data);
	if (!(o_flags & OBJ_NOHEADER)) {
		if (p->o_oid != oid)
			log_warn("OID mismatch: %" PRIx64 " / %" PRIx64 "\n", p->o_oid, oid);
		if (p->o_type != type)
			log_warn("Type mismatch: %08X / %08X\n", p->o_type, type);
		o.m_oid = p->o_oid;
		o.m_xid = p->o_xid;
		o.m_type = p->o_type;
		o.m_subtype = p->o_subtype;
	} else {
		o.m_oid = oid;
		o.m_xid = xid;
		o.m_type = type;
		o.m_subtype = subtype;
	}
	o.m_paddr = paddr;
	o.m_oc = this;
	o.m_fs = fs;

	// log_debug("obj_get %" PRIx64 " %" PRIx64 " -> new oid %" PRIx64 " xid %" PRIx64 " type %x subtype %x paddr %" PRIx64 "\n", oid, xid, o.m_oid, o.m_xid, o.m_type, o.m_subtype, o.m_paddr);

	return 0;
}

void ObjCache::htAdd(Object* obj, uint64_t key)
{
	Object*& head = m_hashtable[key & m_ht_mask];
	if ((obj->m_le_ht.next = head) != nullptr)
		head->m_le_ht.prev = &obj->m_le_ht.next;
	head = obj;
	obj->m_le_ht.prev = &head;
}

void ObjCache::htRemove(Object* obj)
{
	if (obj->m_le_ht.next != nullptr)
		obj->m_le_ht.next->m_le_ht.prev = obj->m_le_ht.prev;
	*obj->m_le_ht.prev = obj->m_le_ht.next;
}

void ObjCache::lruAdd(Object* obj)
{
	obj->m_le_lru.next = nullptr;
	obj->m_le_lru.prev = m_lru_list.last;
	*m_lru_list.last = obj;
	m_lru_list.last = &obj->m_le_lru.next;
	m_lru_cnt++;
}

void ObjCache::lruRemove(Object* obj)
{
	if (obj->m_le_lru.next != 0)
		obj->m_le_lru.next->m_le_lru.prev = obj->m_le_lru.prev;
	else
		m_lru_list.last = obj->m_le_lru.prev;
	*obj->m_le_lru.prev = obj->m_le_lru.next;
	m_lru_cnt--;
}

void ObjCache::lruShrink()
{
	Object* o = m_lru_list.first;
	Object* n;

	while (m_lru_cnt > m_lru_limit && o != nullptr) {
		n = o->m_le_lru.next;
		if (o->m_refcnt == 0) {
			htRemove(o);
			lruRemove(o);
			delete o;
		}
		o = n;
	}
}

void ObjCache::ephemeralAdd(Object* obj)
{
	obj->m_le_lru.next = nullptr;
	obj->m_le_lru.prev = m_ephemeral_list.last;
	*m_ephemeral_list.last = obj;
	m_ephemeral_list.last = &obj->m_le_lru.next;
}

void ObjCache::ephemeralRemove(Object* obj)
{
	if (obj->m_le_lru.next != 0)
		obj->m_le_lru.next->m_le_lru.prev = obj->m_le_lru.prev;
	else
		m_ephemeral_list.last = obj->m_le_lru.prev;
	*obj->m_le_lru.prev = obj->m_le_lru.next;
}

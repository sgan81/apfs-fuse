#include <cerrno>
#include <cstdlib>

#include "DiskStruct.h"
#include "Object.h"
#include "ObjCache.h"
#include "ApfsContainer.h"
#include "ApfsVolume.h"

ObjCache::ObjCache()
{
	m_ht_size = 0x400;
	m_ht_mask = 0x3FF;
	m_hashtable = new Object*[m_ht_size];
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
		delete obj;
		obj = next;
	}

	delete[] m_hashtable;
}

int ObjCache::getObj(Object*& obj, const void* params, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, ApfsVolume* vol)
{
	Object* o;
	int err;

	obj = nullptr;

	for (o = m_hashtable[oid & m_ht_mask]; o != nullptr; o = o->m_le_ht.next) {
		if (oid == o->m_oid && xid == o->m_xid && type == o->m_type) {
			obj = o;
			o->retain();
			lruRemove(o);
			lruAdd(o);
			return 0;
		}
	}

	o = createObjInstance(type);
	if (o == nullptr) return EINVAL;

	err = readObj(*o, oid, xid, type, subtype, 0x1000, paddr); // TODO
	if (err != 0) {
		delete o;
		return err;
	}

	err = o->init(params);
	if (err != 0) {
		delete o;
		return err;
	}

	obj = o;
	htAdd(o, oid);
	lruAdd(o);
	lruShrink();

	return 0;
}

Object * ObjCache::createObjInstance(uint32_t type)
{
	Object* o = nullptr;
	switch (type & OBJECT_TYPE_MASK) {
	case OBJECT_TYPE_BTREE:
		// o = new BTree();
		break;
	case OBJECT_TYPE_BTREE_NODE:
		// o = new BTreeNode();
		break;
		// ...
	}
	return o;
}

int ObjCache::readObj(Object& o, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, ApfsVolume* vol)
{
	o.m_oid = oid;
	o.m_xid = xid;
	o.m_type = type;
	o.m_subtype = subtype;
	o.m_size = size;
	o.m_o = reinterpret_cast<obj_phys_t*>(malloc(size));

	if (paddr == 0) {
		uint32_t cls = type & OBJECT_TYPE_FLAGS_MASK;
		if (cls & OBJ_EPHEMERAL)
			return ENOTSUP;
		else if (cls & OBJ_PHYSICAL)
			paddr = oid;
		else {
			// TODO omap lookup
			/*
			if (vol)
				omap = vol->omap();
			else
				omap = m_nx->omap();
			*/
		}
	}

	if (paddr == 0)
		return ENOENT;

	// if (vol) ... else
	return m_nx->ReadBlocks(reinterpret_cast<uint8_t*>(o.m_o), paddr, size / m_nx->GetBlocksize()) ? 0 : EIO; // TODO
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

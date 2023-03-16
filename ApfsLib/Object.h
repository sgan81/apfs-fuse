#pragma once

#include <cstdint>

#include "ApfsTypes.h"
#include "DiskStruct.h"
#include "ObjCache.h"

struct obj_phys_t;

class Object;
class ObjCache;
class ApfsVolume;
class ApfsContainer;

struct ObjListEntry
{
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

	ApfsContainer* nx() { return m_oc->nx(); }
	ApfsVolume* fs() { return m_fs; }
	ObjCache* oc() { return m_oc; }

private:
	ObjCache* m_oc;
	ApfsVolume* m_fs;

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

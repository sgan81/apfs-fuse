#pragma once

#include <cstdint>

#include "ApfsTypes.h"
#include "DiskStruct.h"

struct obj_phys_t;

class Object;

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
	uint32_t subtype() const { return m_subtype; }

private:
	int m_refcnt;

	obj_phys_t* m_o;
	uint32_t m_size;
	oid_t m_oid;
	xid_t m_xid;
	uint32_t m_type;
	uint32_t m_subtype;

	ObjListEntry m_le_ht;
	ObjListEntry m_le_lru;
};

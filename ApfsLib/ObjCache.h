#pragma once

#include <cstdint>

#include "ApfsTypes.h"

class Object;
class ApfsContainer;
class ApfsVolume;

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

	void setContainer(ApfsContainer* nx);
	int getObj(Object*& obj, const void* params, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, ApfsVolume* fs = nullptr);

	ApfsContainer* nx() { return m_nx; }

private:
	Object* createObjInstance(uint32_t type);
	int readObj(Object& o, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, uint32_t size, paddr_t paddr, ApfsVolume* fs = nullptr);

	void htAdd(Object* obj, uint64_t key);
	void htRemove(Object* obj);
	void lruAdd(Object* obj);
	void lruRemove(Object* obj);
	void lruShrink();

	Object** m_hashtable;
	ObjListHead m_lru_list;
	uint64_t m_ht_size;
	uint64_t m_ht_mask;
	uint32_t m_lru_cnt;
	uint32_t m_lru_limit;
	ApfsContainer* m_nx;
};

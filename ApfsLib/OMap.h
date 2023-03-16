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

private:
	const omap_phys_t* om_phys;
	BTree m_tree;
};

#pragma once

#include "Object.h"
#include "ObjPtr.h"
#include "BTree.h"

class OMap : public Object
{
public:
	OMap();
	virtual ~OMap();

	int init(const void * params) override;

	int lookup(oid_t oid, xid_t xid, xid_t* xid_o, uint32_t* flags, uint32_t* size, paddr_t* paddr);

private:
	BTree* getTree();

	const omap_phys_t* om_phys;
	// ObjPtr<BTree> m_tree;
	BTree* m_tree; // TODO -> ObjPtr
};

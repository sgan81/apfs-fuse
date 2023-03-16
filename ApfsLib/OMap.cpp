#include "OMap.h"
#include "ApfsContainer.h"

OMap::OMap()
{
	om_phys = nullptr;
	m_tree = nullptr;
}

OMap::~OMap()
{
}

int OMap::init(const void* params)
{
	om_phys = reinterpret_cast<const omap_phys_t*>(data());
	return 0;
}

int OMap::lookup(oid_t oid, xid_t xid, xid_t* xid_o, uint32_t* flags, uint32_t* size, paddr_t* paddr)
{
	BTree* tree = getTree();
	if (tree == nullptr)
		return EINVAL;

	omap_key_t ok;
	omap_val_t ov;
	uint16_t key_len = sizeof(omap_key_t);
	uint16_t val_len = sizeof(omap_val_t);
	int err;

	if (xid == 0) xid = UINT64_MAX;

	ok.ok_oid = oid;
	ok.ok_xid = xid;
	err = tree->Lookup(&ok, sizeof(omap_key_t), key_len, &ov, val_len, BTree::FindMode::LE);
	if (err) return err;
	if (ok.ok_oid != oid) return ENOENT;
	if (xid_o) *xid_o = ok.ok_xid;
	if (flags) *flags = ov.ov_flags;
	if (size) *size = ov.ov_size;
	if (paddr) *paddr = ov.ov_paddr;

	// tree->release(); --- nope, ObjPtr
	return 0;
}

BTree * OMap::getTree()
{
	int err;
	Object* tree;

	// Something like this in BTree::Get ...
	err = oc()->getObj(tree, nullptr, om_phys->om_tree_oid, 0, om_phys->om_tree_type, OBJECT_TYPE_OMAP, 0, 0, nullptr);

	// m_tree = tree;
	// retain ...
	return m_tree;
}

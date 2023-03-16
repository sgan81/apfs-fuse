#include <cstdlib>

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

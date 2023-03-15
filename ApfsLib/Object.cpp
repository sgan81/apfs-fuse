#include <cstdlib>

#include "Object.h"

Object::Object()
{
	m_refcnt = 0;
	m_o = nullptr;
	m_size = 0;
	m_oid = 0;
	m_xid = 0;
	m_type = 0;
	m_subtype = 0;
}

Object::~Object()
{
	if (m_o) {
		free(m_o);
	}
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

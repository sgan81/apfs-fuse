#pragma once

#include "Object.h"

template<typename T>
class ObjPtr
{
public:
	ObjPtr() {
		p = nullptr;
	}

	~ObjPtr() {
		if (p) p->release();
	}

	ObjPtr(const ObjPtr& o) {
		p = o.p;
		p->retain();
	}

	void operator=(const ObjPtr& o) {
		p = o.p;
		p->retain();
	}

	Object* operator=(Object* o) {
		if (p) p->release();
		p = o;
		if (p) p->retain();
		return o;
	}

	void reset() {
		if (p) p->release();
		p = nullptr;
	}

	T* operator->() {
		return static_cast<T*>(p);
	}

	const T* operator->() const {
		return static_cast<const T*>(p);
	}

	T* get() {
		return static_cast<T*>(p);
	}

	operator bool() const {
		return p != nullptr;
	}

private:
	Object* p;
};

/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2023 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

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

	T* operator=(T* o) {
		if (p) p->release();
		p = o;
		if (p) p->retain();
		return o;
	}

	T* release() {
		T* r = p;
		if (p) p->release();
		p = nullptr;
		return r;
	}

	T* operator->() {
		return p;
	}

	const T* operator->() const {
		return p;
	}

	T* get() {
		return p;
	}

	T& ref() {
		return *p;
	}

	const T& ref() const {
		return *p;
	}

	operator bool() const {
		return p != nullptr;
	}

private:
	T* p;
};

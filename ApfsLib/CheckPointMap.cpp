/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
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

#include <cassert>

#include "Container.h"
#include "DiskStruct.h"
#include "BlockDumper.h"
#include "CheckPointMap.h"

CheckPointMap::CheckPointMap(Container& container) : m_container(container)
{
	m_cpm_oid = 0;
	m_blksize = 0;
}

CheckPointMap::~CheckPointMap()
{
}

bool CheckPointMap::Init(oid_t root_oid, uint32_t blk_count)
{
	uint32_t n;
	const checkpoint_map_phys_t *cpm;

	m_blksize = m_container.GetBlocksize();
	m_cpm_data.resize(m_blksize * blk_count);

	for (n = 0; n < blk_count; n++)
	{
		if (!m_container.ReadAndVerifyHeaderBlock(m_cpm_data.data() + n * m_blksize, root_oid + n))
		{
			m_cpm_data.clear();
			return false;
		}

		cpm = reinterpret_cast<const checkpoint_map_phys_t *>(m_cpm_data.data() + m_blksize * n);

		assert((cpm->cpm_o.o_type & OBJECT_TYPE_MASK) == OBJECT_TYPE_CHECKPOINT_MAP);

		if ((cpm->cpm_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_CHECKPOINT_MAP)
		{
			m_cpm_data.clear();
			return false;
		}
	}

	m_cpm_oid = root_oid;

	return true;
}

bool CheckPointMap::Lookup(omap_res_t & res, oid_t oid, xid_t xid)
{
	uint32_t blk_offs;
	uint32_t k;
	uint32_t cnt;
	const checkpoint_map_phys_t * cpm;

	(void)xid;

	for (blk_offs = 0; blk_offs < m_cpm_data.size(); blk_offs += m_blksize)
	{
		cpm = reinterpret_cast<const checkpoint_map_phys_t *>(m_cpm_data.data() + blk_offs);

		cnt = cpm->cpm_count;

		for (k = 0; k < cnt; k++)
		{
			if (oid == cpm->cpm_map[k].cpm_oid)
			{
				res.oid = cpm->cpm_map[k].cpm_oid;
				res.xid = cpm->cpm_o.o_xid;
				res.flags = 0;
				res.size = cpm->cpm_map[k].cpm_size;
				res.paddr = cpm->cpm_map[k].cpm_paddr;

				return true;
			}
		}
	}

	return false;
}

void CheckPointMap::dump(BlockDumper& bd)
{
	bd.DumpNode(m_cpm_data.data(), m_cpm_oid);
}

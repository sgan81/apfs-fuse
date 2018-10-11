#include <cassert>

#include "ApfsContainer.h"
#include "DiskStruct.h"
#include "BlockDumper.h"
#include "CheckPointMap.h"

CheckPointMap::CheckPointMap(ApfsContainer& container) : m_container(container)
{
	m_cpm_oid = 0;
	m_blksize = 0;
}

CheckPointMap::~CheckPointMap()
{
}

bool CheckPointMap::Init(uint64_t root_oid, uint32_t blk_count)
{
	uint32_t n;
	m_blksize = m_container.GetBlocksize();
	const checkpoint_map_phys_t *cpm;

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

bool CheckPointMap::GetBlockID(node_info_t& info, uint64_t oid, uint64_t xid)
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
				info.bid = cpm->cpm_map[k].cpm_paddr;
				info.flags = 0;
				info.size = cpm->cpm_map[k].cpm_size;

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

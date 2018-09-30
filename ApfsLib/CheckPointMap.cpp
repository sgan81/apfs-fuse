#include "ApfsContainer.h"
#include "DiskStruct.h"
#include "BlockDumper.h"
#include "CheckPointMap.h"

CheckPointMap::CheckPointMap(ApfsContainer& container) : m_container(container)
{
	m_cpm = 0;
	m_cpm_oid = 0;
}

CheckPointMap::~CheckPointMap()
{
}

bool CheckPointMap::Init(uint64_t root_oid)
{
	m_cpm_data.resize(m_container.GetBlocksize());

	if (!m_container.ReadAndVerifyHeaderBlock(m_cpm_data.data(), root_oid))
		return false;

	m_cpm = reinterpret_cast<const checkpoint_map_phys_t *>(m_cpm_data.data());

	if ((m_cpm->cpm_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_CHECKPOINT_MAP)
		return false;

	m_cpm_oid = root_oid;

	return true;
}

bool CheckPointMap::GetBlockID(node_info_t& info, uint64_t oid, uint64_t xid)
{
	uint32_t k;
	uint32_t cnt;

	(void)xid;

	cnt = m_cpm->cpm_count;

	for (k = 0; k < cnt; k++)
	{
		if (oid == m_cpm->cpm_map[k].cpm_oid)
		{
			info.bid = m_cpm->cpm_map[k].cpm_paddr;
			info.flags = 0;
			// info.flags = m_cpm->cpm_map[k].cpm_unk_C; // Only place where there could be flags ...
			info.size = m_cpm->cpm_map[k].cpm_size;

			return true;
		}
	}

	return false;
}

void CheckPointMap::dump(BlockDumper& bd)
{
	bd.DumpNode(m_cpm_data.data(), m_cpm_oid);
}

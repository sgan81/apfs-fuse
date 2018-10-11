#pragma once

#include <vector>

#include "Endian.h"
#include "Device.h"


class DeviceVDI : public Device
{
public:
	DeviceVDI();
	~DeviceVDI();

	bool Open(const char *name) override;
	void Close() override;

	bool Read(void *data, uint64_t offs, uint64_t len) override;
	uint64_t GetSize() const override;

private:
	uint64_t m_disk_size;
	uint32_t m_block_size;
	uint32_t m_block_count;
	uint32_t m_data_offset;

	std::vector<uint32_t> m_block_map;

	FILE *m_vdi;
};

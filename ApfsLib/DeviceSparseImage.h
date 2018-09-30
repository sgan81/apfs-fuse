#pragma once

#include <vector>

#include "Device.h"
#include "DiskImageFile.h"

class DeviceSparseImage : public Device
{
public:
	DeviceSparseImage();
	~DeviceSparseImage();

	bool Open(const char *name) override;
	void Close() override;

	bool Read(void *data, uint64_t offs, uint64_t len) override;
	uint64_t GetSize() const override;

private:
	std::vector<uint64_t> m_chunk_offs;
	uint64_t m_size;
	uint64_t m_chunk_size;

	DiskImageFile m_img;
};

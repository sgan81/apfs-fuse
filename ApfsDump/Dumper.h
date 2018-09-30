#pragma once

#include <vector>
#include <iosfwd>

#include <ApfsLib/AesXts.h>

extern volatile bool g_abort; // In Apfs.cpp

class Device;

class Dumper
{
public:
	Dumper(Device &dev);
	~Dumper();

	bool Initialize();
	bool DumpContainer(std::ostream &os);
	bool DumpBlockList(std::ostream &os);

private:
	bool Read(void *data, uint64_t paddr, uint64_t cnt);
	bool Read(std::vector<uint8_t> &data, uint64_t paddr, uint64_t cnt);

	void Decrypt(uint8_t *data, size_t size, uint64_t paddr);

	uint64_t cpm_lookup(const checkpoint_map_phys_t *cpm, uint64_t oid);

	Device &m_dev;
	uint64_t m_partbase;
	uint64_t m_partsize;
	uint32_t m_blocksize;

	AesXts m_aes;
	bool m_is_encrypted;
};

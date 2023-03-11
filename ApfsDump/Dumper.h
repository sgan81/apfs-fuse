#pragma once

#include <vector>
#include <iosfwd>

#include <Crypto/AesXts.h>

extern volatile bool g_abort; // In Apfs.cpp

class Device;

class Dumper
{
public:
	Dumper(Device *dev_main, Device *dev_tier2);
	~Dumper();

	bool Initialize();
	bool DumpContainer(std::ostream &os);
	bool DumpBlockList(std::ostream &os);

private:
	bool Read(void *data, uint64_t paddr, uint64_t cnt);
	bool Read(std::vector<uint8_t> &data, uint64_t paddr, uint64_t cnt);

	void Decrypt(uint8_t *data, size_t size, uint64_t paddr);

	const checkpoint_mapping_t* cpm_lookup(const checkpoint_map_phys_t *cpm, uint64_t oid);

	Device *m_dev_main;
	Device *m_dev_tier2;
	uint64_t m_base_main;
	uint64_t m_size_main;
	uint64_t m_base_tier2;
	uint64_t m_size_tier2;
	uint32_t m_blocksize;

	AesXts m_aes;
	bool m_is_encrypted;
};

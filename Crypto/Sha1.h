#pragma once

#include <cstddef>
#include <cstdint>

class Sha1
{
public:
	Sha1();
	~Sha1();

	void Init();
	void Update(const void *data, size_t size);
	void Final(uint8_t *hash);

private:
	void Round();

	uint8_t m_buffer[64];
	uint32_t m_hash[5];
	uint64_t m_bit_cnt;
	size_t m_buf_idx;

	static const uint32_t m_K[4];
};


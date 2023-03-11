#pragma once

#include <cstddef>
#include <cstdint>

class SHA256
{
public:
	SHA256();
	~SHA256();

	void Init();
	void Update(const void *data, size_t size);
	void Final(uint8_t *hash);

private:
	void Round();

	uint8_t m_buffer[64];
	uint32_t m_hash[8];
	uint32_t m_bufferPtr;
	size_t m_byteCnt;

	static const uint32_t m_k[64];
};

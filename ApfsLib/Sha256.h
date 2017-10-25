#pragma once

#include <cstddef>
#include <cstdint>

class SHA256 {
public:
	SHA256();
	~SHA256();

	void Init();
	void Update(const void *data, size_t size);
	void Final(uint8_t *hash);

private:
	void Round();

	uint8_t _buffer[64];
	uint32_t _hash[8];
	uint32_t _bufferPtr;
	size_t _byteCnt;

	static const uint32_t _k[64];
};

#include <cstdio>
#include <cinttypes>
#include <cstring>

#include "Asn1Der.h"

const uint8_t* der_decode_tag(der_tag_t& tag, const uint8_t* der, const uint8_t* der_end)
{
	uint64_t t;
	uint8_t b;
	uint8_t flg;

	tag = 0;

	if (der == nullptr || der >= der_end)
		return nullptr;
	flg = *der++;

	if ((flg & 0x1F) == 0x1F) {
		t = 0;
		do {
			if (der >= der_end) return nullptr;
			b = *der++;
			t = t << 7 | (b & 0x7F);
		} while (b & 0x80);
		tag = static_cast<uint64_t>(flg & 0xE0) << 56 | (t & 0x1FFFFFFFFFFFFFFFU);
	}
	else {
		tag = static_cast<uint64_t>(flg & 0xE0) << 56 | (flg & 0x1F);
	}
	return der;
}

const uint8_t* der_decode_len(size_t& len, const uint8_t* der, const uint8_t* der_end)
{
	uint64_t s;
	uint8_t nb;
	unsigned k;

	len = 0;

	if (der == nullptr || der >= der_end)
		return nullptr;
	nb = *der++;

	if (nb & 0x80) {
		s = 0;
		nb &= 0x7F;
		if ((der + nb) >= der_end) return nullptr;
		for (k = 0; k < nb; k++)
			s = s << 8 | *der++;
		len = s;
	}
	else {
		len = nb;
	}
	return der;
}

const uint8_t* der_decode_tl(der_tag_t expected_tag, size_t& len, const uint8_t* der, const uint8_t* der_end)
{
	der_tag_t tag;
	der = der_decode_tag(tag, der, der_end);
	if (tag != expected_tag)
		return nullptr;
	der = der_decode_len(len, der, der_end);
	if (der + len > der_end)
		return nullptr;
	return der;
}

const uint8_t* der_decode_constructed_tl(der_tag_t expected_tag, const uint8_t*& body_end, const uint8_t* der, const uint8_t* der_end)
{
	size_t len;
	der = der_decode_tl(expected_tag, len, der, der_end);
	if (der == nullptr)
		return nullptr;
	body_end = der + len;
	return der;
}

const uint8_t* der_decode_sequence_tl(const uint8_t*& body_end, const uint8_t* der, const uint8_t* der_end)
{
	return der_decode_constructed_tl(DER_CONSTRUCTED | DER_SEQUENCE, body_end, der, der_end);
}

const uint8_t* der_decode_uint(size_t n, uint64_t& val, const uint8_t* der, const uint8_t* der_end)
{
	uint64_t v = 0;
	size_t k;

	val = 0;
	if (der == nullptr || (der + n) > der_end)
		return nullptr;
	for (k = 0; k < n; k++)
		v = v << 8 | *der++;
	val = v;
	return der;
}

const uint8_t * der_decode_uint64(der_tag_t expected_tag, uint64_t& val, const uint8_t* der, const uint8_t* der_end)
{
	size_t len;

	val = 0;
	der = der_decode_tl(expected_tag, len, der, der_end);
	if (der == nullptr || len > 8) return nullptr;
	der = der_decode_uint(len, val, der, der_end);
	return der;
}

const uint8_t * der_decode_octet_string_copy(der_tag_t expected_tag, uint8_t* buf, size_t len, const uint8_t* der, const uint8_t* der_end)
{
	size_t slen;

	der = der_decode_tl(expected_tag, slen, der, der_end);
	if (der == nullptr || slen != len || (der + slen) > der_end) return nullptr;
	memcpy(buf, der, slen);
	return der + slen;
}

/*
static void der_dump_data(const uint8_t* data, size_t len)
{
	size_t y;
	size_t x;
	constexpr size_t ls = 32;
	size_t ll;

	for (y = 0; y < len; y += ls) {
		ll = ls;
		if ((len - y) < ls) ll = len - y;
		for (x = 0; x < (ll - 1); x++)
			printf("%02X ", data[y + x]);
		printf("%02X\n", data[y + x]);
	}
}
*/

static void der_dump_hex(const uint8_t* data, size_t len)
{
	size_t n;

	for (n = 0; n < len; n++)
		printf("%02X", data[n]);
	printf("\n");
}

static void der_dump_internal(const uint8_t* der, const uint8_t* der_end, int indent)
{
	der_tag_t tag;
	size_t len;
	int n;
	// const uint8_t* der_start = der;
	// printf("DER %zX\n", der_end - der);

	while (der < der_end && der != nullptr) {
		// printf("[%04zX", der - der_start);
		der = der_decode_tag(tag, der, der_end);
		der = der_decode_len(len, der, der_end);
		// printf("/%04zX] ", der - der_start);
		if (der == nullptr || (der + len) > der_end) break;
		for (n = 0; n < indent; n++)
			putchar(' ');
		printf("%016" PRIX64  " %04zX", tag, len);
		if (tag & DER_CONSTRUCTED) {
			printf("\n");
			der_dump_internal(der, der + len, indent + 2);
		} else {
			printf(" : ");
			der_dump_hex(der, len);
		}
		der += len;
	}

	if (der == nullptr)
		printf("Malformed ASN.1 DER\n");
}

void der_dump(const uint8_t* data, size_t size)
{
	// der_dump_data(data, size);
	der_dump_internal(data, data + size, 0);
}

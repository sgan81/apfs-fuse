#pragma once

#include <cstdint>
#include <cstddef>

typedef uint64_t der_tag_t;

static constexpr der_tag_t DER_CONSTRUCTED = 0x2000000000000000U;
static constexpr der_tag_t DER_CONTEXT_SPECIFIC = 0x8000000000000000U;

enum der_tag_type_t
{
	DER_BOOLEAN = 1,
	DER_INTEGER = 2,
	DER_BIT_STRING = 3,
	DER_OCTET_STRING = 4,
	DER_NULL = 5,
	DER_OBJECT_IDENTIFIER = 6,
	DER_OBJECT_DESCRIPTOR = 7,
	DER_EXTERNAL = 8,
	DER_REAL = 9,
	DER_ENUMERATED = 10,
	DER_EMBEDDED_PDV = 11,
	DER_UTF8String = 12,
	DER_RELATIVE_OID = 13,
	DER_SEQUENCE = 16,
	DER_SET = 17,
	DER_NumericString = 18,
	DER_PrintableString = 19,
	DER_TeletexString = 20,
	DER_VideotexString = 21,
	DER_IA5String = 22,
	DER_UTCTime = 23,
	DER_GeneralizedTime = 24,
	DER_GraphicString = 25,
	DER_VisibleString = 26,
	DER_GeneralString = 27,
	DER_UniversalString = 28,
	DER_CHARACTER_STRING = 29,
	DER_BMPString = 30
};

const uint8_t* der_decode_tag(der_tag_t& tag, const uint8_t* der, const uint8_t* der_end);
const uint8_t* der_decode_len(size_t& len, const uint8_t* der, const uint8_t* der_end);
const uint8_t* der_decode_tl(der_tag_t expected_tag, size_t& len, const uint8_t* der, const uint8_t* der_end);
const uint8_t* der_decode_constructed_tl(der_tag_t expected_tag, const uint8_t*& body_end, const uint8_t* der, const uint8_t* der_end);
const uint8_t* der_decode_sequence_tl(const uint8_t*& body_end, const uint8_t* der, const uint8_t* der_end);
const uint8_t* der_decode_uint(size_t n, uint64_t& val, const uint8_t* der, const uint8_t* der_end);

const uint8_t* der_decode_uint64(der_tag_t expected_tag, uint64_t& val, const uint8_t* der, const uint8_t* der_end);
const uint8_t* der_decode_octet_string_copy(der_tag_t expected_tag, uint8_t* buf, size_t len, const uint8_t* der, const uint8_t* der_end);

void der_dump(const uint8_t* data, size_t size);

#pragma once

#include <cstdint>
#include <vector>

int normalizeOptFoldU32Char(char32_t ch, bool case_insensitive, char32_t *sequence_out, unsigned char *unknown_out);
void CanonicalReorder(char32_t *nfd, uint8_t *ccc, size_t len);

bool NormalizeFoldString(std::vector<char32_t> &out, const std::vector<char32_t> &in, bool case_fold);

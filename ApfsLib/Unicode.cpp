/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Unicode.h"

#include <cstdint>
#include <cstddef>

#include "UnicodeTables_v10.h"

int normalizeJimdo(char32_t ch, char32_t *nfd, uint8_t *ccc)
{
	constexpr int SBase = 0xAC00;
	constexpr int LBase = 0x1100;
	constexpr int VBase = 0x1161;
	constexpr int TBase = 0x11A7;
	// constexpr int LCount = 19;
	constexpr int VCount = 21;
	constexpr int TCount = 28;
	constexpr int NCount = VCount * TCount;
	// constexpr int SCount = LCount * NCount;

	int SIndex = ch - SBase;
	int LIndex = SIndex / NCount;
	int VIndex = (SIndex % NCount) / TCount;
	int TIndex = SIndex % TCount;

	nfd[0] = LBase + LIndex;
	ccc[0] = 0;
	nfd[1] = VBase + VIndex;
	ccc[1] = 0;
	if (TIndex > 0)
	{
		nfd[2] = TBase + TIndex;
		ccc[2] = 0;
		return 3;
	}

	return 2;
}

int normalizeOptFoldU32Char(char32_t ch, bool case_fold, char32_t *nfd, uint8_t *ccc)
{
	char32_t ch_idx;
	uint16_t hi_res;
	uint16_t mi_res;
	uint16_t lo_res;
	const uint16_t *seq_u16 = 0;
	const uint32_t *seq_u32 = 0;
	uint32_t seq_len = 0;
	uint32_t cnt;
	char32_t c;

	ccc[0] = 0;
	if (ch >= 0xF0000)
	{
		if ((ch & 0xFFFE) == 0xFFFE)
			return -1;
		else
		{
			nfd[0] = ch;
			return 1;
		}
	}

	if (ch < 0x2FB00)
		ch_idx = ch;
	else if ((ch & 0xFFFFFE00) == 0xE0000)
		ch_idx = ch - 0xB0500;
	else
		return -1;

	hi_res = nf_trie_hi[ch_idx >> 8];

	if (hi_res == 0xFFFF)
		return -1;
	if (hi_res == 0 || ((hi_res & 0xFF00) == 0xAD00))
	{
		nfd[0] = ch;
		ccc[0] = hi_res & 0xFF;
		return 1;
	}

	if (hi_res == 0xAC00) // Naja, fast ... sollte funktionieren
		return normalizeJimdo(ch, nfd, ccc);

	mi_res = nf_trie_mid[((hi_res & 0xFFF) << 4) | ((ch_idx >> 4) & 0xF)];

	if (mi_res == 0xFFFF)
		return -1;

	if (mi_res == 0xAC00)
		return normalizeJimdo(ch, nfd, ccc);

	if (mi_res == 0 || (mi_res & 0xFF00) == 0xAD00)
	{
		ccc[0] = mi_res & 0xFF;
		if (case_fold && (ch < 0x500))
			nfd[0] = nf_basic_cf[ch];
		else
			nfd[0] = ch;
		return 1;
	}

	if ((mi_res & 0xFF00) == 0xAE00)
	{
		uint16_t mask = nf_u16_inv_masks[mi_res & 0xFF];
		if ((mask >> (ch_idx & 0xF)) & 1)
			return -1;
		if (case_fold && (ch < 0x500))
			nfd[0] = nf_basic_cf[ch];
		else
			nfd[0] = ch;
		return 1;
	}

	lo_res = nf_trie_lo[((mi_res & 0xFFF) << 4) | (ch_idx & 0xF)];

	if (lo_res == 0xFFFF)
		return -1;

	if (lo_res == 0xAC00)
		return normalizeJimdo(ch, nfd, ccc);

	if (lo_res < 0xB000 || lo_res >= 0xF900)
	{
		if (lo_res == 0 || ((lo_res & 0xFF00) == 0xAD00))
			ccc[0] = lo_res & 0xFF;
		else
			ch = lo_res;

		if (case_fold && (ch < 0x500))
			nfd[0] = nf_basic_cf[ch];
		else
			nfd[0] = ch;
		return 1;
	}

	switch ((lo_res >> 12) & 0xF)
	{
	case 0xB:
		if ((lo_res & 0x800) && !case_fold)
		{
			nfd[0] = ch;
			return 1;
		}

		seq_u16 = nf_u16_seq_2 + 2 * (lo_res & 0x7FF);
		seq_len = 2;
		break;

	case 0xC:
		if ((lo_res & 0x800) && !case_fold)
		{
			nfd[0] = ch;
			return 1;
		}

		seq_u16 = nf_u16_seq_3 + 3 * (lo_res & 0x7FF);
		seq_len = 3;
		break;

	case 0xD:
		seq_u16 = nf_u16_seq_misc + (lo_res & 0x3FF) + 1;
		seq_len = nf_u16_seq_misc[lo_res & 0x3FF]; // Rest >> 4 in eax
		ccc[0] = seq_len >> 4;
		seq_len &= 0xF;
		if (seq_len > 4)
			return 0;
		break;

	case 0xE:
		if ((lo_res & 0x800) && !case_fold)
		{
			nfd[0] = ch;
			return 1;
		}

		seq_u32 = nf_u32_char + (lo_res & 0x7FF);
		seq_len = 1;
		break;

	case 0xF:
		seq_u32 = nf_u32_seq_misc + (lo_res & 0x3FF) + 1;
		seq_len = nf_u32_seq_misc[lo_res & 0x3FF];
		ccc[0] = seq_len >> 4;
		seq_len &= 0xF;
		if (seq_len > 4)
			return 0;
		break;
	}

	for (cnt = 0; cnt < seq_len; cnt++)
	{
		if (seq_u16)
			c = seq_u16[cnt];
		else
			c = seq_u32[cnt];
		nfd[cnt] = c;

		if (cnt > 0)
		{
			if (c >= 0xF0000)
			{
				ccc[cnt] = 0;
				continue;
			}

			if (c == 0x3B9)
			{
				ccc[cnt] = 0xF0;
				continue;
			}

			ch_idx = (c > 0x2FB00) ? (c - 0xB0500) : c;

			hi_res = nf_trie_hi[ch_idx >> 8];

			if (hi_res == 0 || ((hi_res & 0xFF00) == 0xAD00))
			{
				ccc[cnt] = hi_res & 0xFF;
				continue;
			}

			mi_res = nf_trie_mid[((hi_res & 0xFFF) << 4) | ((ch_idx >> 4) & 0xF)];

			if (mi_res == 0 || ((mi_res & 0xFF00) == 0xAE00))
			{
				ccc[cnt] = 0;
				continue;
			}
			if ((mi_res & 0xFF00) == 0xAD00)
			{
				ccc[cnt] = mi_res & 0xFF;
				continue;
			}

			lo_res = nf_trie_lo[((mi_res & 0xFFF) << 4) | (ch_idx & 0xF)];

			if ((lo_res & 0xFF00) == 0xAD00)
				ccc[cnt] = lo_res & 0xFF;
			else
				ccc[cnt] = 0;
		}
	}

	if (case_fold)
	{
		if (nfd[0] < 0x500)
			nfd[0] = nf_basic_cf[nfd[0]];

		if (cnt >= 2)
		{
			if (nfd[cnt - 1] == 0x345)
				nfd[cnt - 1] = 0x3B9;
		}
	}

	return cnt;
}

void CanonicalReorder(char32_t *nfd, uint8_t *ccc, size_t len)
{
	size_t i;
	size_t k;
	bool sorted = false;
	uint8_t sort_ccc;
	char32_t sort_ch;

	for (k = 0; k < (len - 1); k++)
	{
		if (ccc[k] == 0)
			continue;

		if (ccc[k + 1] == 0)
			continue;

		do
		{
			sorted = true;

			for (i = k; i < (len - 1) && ccc[i + 1] != 0; i++)
			{
				if (ccc[i] > ccc[i + 1])
				{
					sorted = false;
					sort_ccc = ccc[i + 1];
					ccc[i + 1] = ccc[i];
					ccc[i] = sort_ccc;
					sort_ch = nfd[i + 1];
					nfd[i + 1] = nfd[i];
					nfd[i] = sort_ch;
				}
			}
		} while (!sorted);

		k = i + 1;
	}
}

bool NormalizeFoldString(std::vector<char32_t> &nfd, const std::vector<char32_t> &in, bool case_fold)
{
	char32_t ch;
	size_t k;
	size_t out_size;
	int rc;

	std::vector<uint8_t> ccc;

	nfd.clear();
	nfd.resize(in.size() * 4);
	ccc.resize(nfd.size());

	for (k = 0, out_size = 0; k < in.size(); k++)
	{
		ch = in[k];
		rc = normalizeOptFoldU32Char(ch, case_fold, nfd.data() + out_size, ccc.data() + out_size);

		if (rc == -1)
			return false;

		out_size += rc;
	}

	nfd.resize(out_size);
	ccc.resize(out_size);

	CanonicalReorder(nfd.data(), ccc.data(), nfd.size());

	return true;
}

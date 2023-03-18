/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2023 Simon Gander
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

#pragma once

#include <cstdint>

class BTree;

void dbg_print_hex(const void* data, size_t size);
void dbg_dump_hex(const void* data, size_t size, size_t lsize = 32);
void dbg_dump_hex_nz(const void* data, size_t size, size_t lsize = 32);

void dbg_print_btree_key(const void* key, uint16_t key_len, uint32_t tree_subtype, bool fs_hashed = true);
void dbg_print_btree_entry(const void* key, uint16_t key_len, const void* val, uint16_t val_len, uint32_t tree_subtype, bool fs_hashed);

void dbg_dump_btree(BTree& tree);

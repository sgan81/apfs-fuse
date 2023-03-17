#pragma once

#include <cstdint>

void dbg_print_btree_key(const void* key, uint16_t key_len, uint32_t tree_subtype, bool fs_hashed = true);
void dbg_print_btree_entry(const void* key, uint16_t key_len, const void* val, uint16_t val_len, uint32_t tree_subtype, bool fs_hashed);

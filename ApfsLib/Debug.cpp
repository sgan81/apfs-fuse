#include <cinttypes>

#include "DiskStruct.h"
#include "Util.h"
#include "Debug.h"

void dbg_print_btkey_fs(const void* key, uint16_t key_len, bool hashed)
{
	const j_key_t* jk = reinterpret_cast<const j_key_t*>(key);

	log_debug("%" PRIx64 "/%" PRIx64, jk->obj_id_and_type >> OBJ_TYPE_SHIFT, jk->obj_id_and_type & OBJ_ID_MASK);

	switch (jk->obj_id_and_type >> OBJ_TYPE_SHIFT) {
	case APFS_TYPE_EXTENT:
	{
	}
	case APFS_TYPE_XATTR:
	{
		const j_xattr_key_t* k = reinterpret_cast<const j_xattr_key_t*>(key);
		log_debug(" %d %s", k->name_len, k->name);
		break;
	}
	case APFS_TYPE_SIBLING_LINK:
	{
		const j_sibling_key_t* k = reinterpret_cast<const j_sibling_key_t*>(key);
		log_debug(" %" PRIx64, k->sibling_id);
		break;
	}
	case APFS_TYPE_FILE_EXTENT:
	{
		const j_file_extent_key_t* k = reinterpret_cast<const j_file_extent_key_t*>(key);
		log_debug(" %" PRIx64, k->logical_addr);
		break;
	}
	case APFS_TYPE_DIR_REC:
	{
		if (hashed) {
			const j_drec_hashed_key_t* k = reinterpret_cast<const j_drec_hashed_key_t*>(key);
			log_debug(" %08X %s", k->name_len_and_hash, k->name);
		} else {
			const j_drec_key_t* k = reinterpret_cast<const j_drec_key_t*>(key);
			log_debug(" %d %s", k->name_len, k->name);
		}
		break;
	}
	default:
		if (key_len != 8)
			log_debug(" [!!! NOT IMPLEMENTED !!!]");
		break;
	}

	log_debug("\n");
}

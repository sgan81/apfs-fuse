#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/BTree.h>

#include <cstdlib>
#include <iostream>

static void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << std::endl;
		std::exit(1);
	}
}

int main()
{
	ApfsContainer container(nullptr, 0, 0);
	BTree tree(container);
	BTreeIterator iterator;
	require(!tree.GetIteratorBegin(iterator), "uninitialized tree rejects iteration");

	std::vector<uint8_t> block(4096);
	auto *header = reinterpret_cast<btree_node_phys_t *>(block.data());
	header->btn_flags = BTNODE_LEAF;
	header->btn_nkeys = 1;
	header->btn_table_space.len = sizeof(kvloc_t);
	auto *entry = reinterpret_cast<kvloc_t *>(header->btn_data);
	entry->k.len = sizeof(uint64_t);
	entry->v.off = sizeof(uint64_t);
	entry->v.len = sizeof(uint64_t);
	auto node = BTreeNode::CreateNode(tree, block.data(), block.size(), 1, nullptr, 0);
	iterator.Setup(&tree, node, 0);
	BTreeEntry result;
	require(iterator.GetEntry(result), "iterator contains an entry before failure");

	require(!tree.Init(0, 0), "zero root rejects initialization");
	require(!tree.GetIteratorBegin(iterator), "failed initialization rejects iteration");
	require(!iterator.GetEntry(result), "failed iteration clears the previous entry");
	return 0;
}

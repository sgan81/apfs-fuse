enable_language(C ASM)
add_library(ApfsLib ApfsContainer.cpp ApfsDir.cpp ApfsNodeMapper.cpp ApfsNodeMapperBTree.cpp ApfsVolume.cpp BlockDumper.cpp BTree.cpp Crc32.cpp Decmpfs.cpp Disk.cpp Inflate.cpp Util.cpp lzvn_decode.s)

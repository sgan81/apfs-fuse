enable_language(C ASM)
add_library(ApfsLib Aes.cpp AesXts.cpp ApfsContainer.cpp ApfsDir.cpp ApfsNodeMapper.cpp ApfsNodeMapperBTree.cpp ApfsVolume.cpp BlockDumper.cpp BTree.cpp Crc32.cpp Crypto.cpp Decmpfs.cpp Device.cpp DeviceLinux.cpp Inflate.cpp KeyMgmt.cpp Sha256.cpp Util.cpp lzvn_decode.s)

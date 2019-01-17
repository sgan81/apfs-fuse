cmake_minimum_required(VERSION 3.0)
# set(CMAKE_C_COMPILER "clang")
# set(CMAKE_CXX_COMPILER "clang++")

project(Apfs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -march=native")

include_directories(. 3rdparty/lzfse/src)

add_library(lzfse
	3rdparty/lzfse/src/lzfse.h
	3rdparty/lzfse/src/lzfse_decode.c
	3rdparty/lzfse/src/lzfse_decode_base.c
	3rdparty/lzfse/src/lzfse_encode.c
	3rdparty/lzfse/src/lzfse_encode_base.c
	3rdparty/lzfse/src/lzfse_encode_tables.h
	3rdparty/lzfse/src/lzfse_fse.c
	3rdparty/lzfse/src/lzfse_fse.h
	3rdparty/lzfse/src/lzfse_internal.h
	3rdparty/lzfse/src/lzfse_tunables.h
	3rdparty/lzfse/src/lzvn_decode_base.c
	3rdparty/lzfse/src/lzvn_decode_base.h
	3rdparty/lzfse/src/lzvn_encode_base.c
	3rdparty/lzfse/src/lzvn_encode_base.h
	)

add_library(apfs
	ApfsLib/Aes.cpp
	ApfsLib/Aes.h
	ApfsLib/AesXts.cpp
	ApfsLib/AesXts.h
	ApfsLib/ApfsContainer.cpp
	ApfsLib/ApfsContainer.h
	ApfsLib/ApfsDir.cpp
	ApfsLib/ApfsDir.h
	ApfsLib/ApfsNodeMapper.cpp
	ApfsLib/ApfsNodeMapper.h
	ApfsLib/ApfsNodeMapperBTree.cpp
	ApfsLib/ApfsNodeMapperBTree.h
	ApfsLib/ApfsVolume.cpp
	ApfsLib/ApfsVolume.h
	ApfsLib/BlockDumper.cpp
	ApfsLib/BlockDumper.h
	ApfsLib/BTree.cpp
	ApfsLib/BTree.h
	ApfsLib/CheckPointMap.cpp
	ApfsLib/CheckPointMap.h
	ApfsLib/Crc32.cpp
	ApfsLib/Crc32.h
	ApfsLib/Crypto.cpp
	ApfsLib/Crypto.h
	ApfsLib/Decmpfs.cpp
	ApfsLib/Decmpfs.h
	ApfsLib/Des.cpp
	ApfsLib/Des.h
	ApfsLib/Device.cpp
	ApfsLib/Device.h
	ApfsLib/DeviceDMG.cpp
	ApfsLib/DeviceDMG.h
	ApfsLib/DeviceLinux.cpp
	ApfsLib/DeviceLinux.h
	ApfsLib/DeviceMac.cpp
	ApfsLib/DeviceMac.h
	ApfsLib/DeviceSparseImage.cpp
	ApfsLib/DeviceSparseImage.h
	ApfsLib/DeviceWinFile.cpp
	ApfsLib/DeviceWinFile.h
	ApfsLib/DeviceWinPhys.cpp
	ApfsLib/DeviceWinPhys.h
	ApfsLib/DiskImageFile.cpp
	ApfsLib/DiskImageFile.h
	ApfsLib/DiskStruct.h
	ApfsLib/Endian.h
	ApfsLib/Global.h
	ApfsLib/GptPartitionMap.cpp
	ApfsLib/GptPartitionMap.h
	ApfsLib/KeyMgmt.cpp
	ApfsLib/KeyMgmt.h
	ApfsLib/PList.cpp
	ApfsLib/PList.h
	ApfsLib/Sha1.cpp
	ApfsLib/Sha1.h
	ApfsLib/Sha256.cpp
	ApfsLib/Sha256.h
	ApfsLib/TripleDes.cpp
	ApfsLib/TripleDes.h
	ApfsLib/Util.cpp
	ApfsLib/Util.h
	ApfsLib/Unicode.cpp
	ApfsLib/Unicode.h)
target_link_libraries(apfs z bz2 lzfse)

add_executable(apfs-dump
	ApfsDump/Dumper.cpp
	ApfsDump/Dumper.h
	ApfsDump/Apfs.cpp)
target_link_libraries(apfs-dump apfs)

add_executable(apfs-dump-quick
	ApfsDumpQuick/ApfsDumpQuick.cpp)
target_link_libraries(apfs-dump-quick apfs)

add_executable(apfs-fuse
	apfsfuse/ApfsFuse.cpp)
target_compile_definitions(apfs-fuse PRIVATE _FILE_OFFSET_BITS=64 _DARWIN_USE_64_BIT_INODE)
if (APPLE)
target_include_directories(apfs-fuse PRIVATE /usr/local/include/osxfuse/)
# link_directories(/usr/local/lib/)
target_link_libraries(apfs-fuse apfs /usr/local/lib/libosxfuse.dylib)
else()
target_link_libraries(apfs-fuse apfs fuse)
endif()

# install(TARGETS apfs-fuse RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

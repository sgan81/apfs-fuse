cmake_minimum_required(VERSION 3.0)
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(Apfs)

option(USE_FUSE3 "Use the FUSE 3 library (required on 32-bit systems)" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_BUILD_TYPE Release)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -march=native -fsanitize=undefined")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -march=native -fsanitize=undefined")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")

# set(BUILD_SHARED_LIBS ON)

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

add_library(crypto
	Crypto/Aes.cpp
	Crypto/Aes.h
	Crypto/AesXts.cpp
	Crypto/AesXts.h
	Crypto/Asn1Der.cpp
	Crypto/Asn1Der.h
	Crypto/Crypto.cpp
	Crypto/Crypto.h
	Crypto/Des.cpp
	Crypto/Des.h
	Crypto/Sha1.cpp
	Crypto/Sha1.h
	Crypto/Sha256.cpp
	Crypto/Sha256.h
	Crypto/TripleDes.cpp
	Crypto/TripleDes.h
)

add_library(apfs
	ApfsLib/ApfsDir.cpp
	ApfsLib/ApfsDir.h
	ApfsLib/BlockDumper.cpp
	ApfsLib/BlockDumper.h
	ApfsLib/BTree.cpp
	ApfsLib/BTree.h
	ApfsLib/Container.cpp
	ApfsLib/Container.h
	ApfsLib/Crc32.cpp
	ApfsLib/Crc32.h
	ApfsLib/Debug.cpp
	ApfsLib/Debug.h
	ApfsLib/Decmpfs.cpp
	ApfsLib/Decmpfs.h
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
	ApfsLib/ObjCache.cpp
	ApfsLib/ObjCache.h
	ApfsLib/Object.cpp
	ApfsLib/Object.h
	ApfsLib/OMap.cpp
	ApfsLib/OMap.h
	ApfsLib/PList.cpp
	ApfsLib/PList.h
	ApfsLib/Spaceman.cpp
	ApfsLib/Spaceman.h
	ApfsLib/Util.cpp
	ApfsLib/Util.h
	ApfsLib/Unicode.cpp
	ApfsLib/Unicode.h
	ApfsLib/Volume.cpp
	ApfsLib/Volume.h
)
target_link_libraries(apfs z bz2 lzfse crypto)
target_compile_definitions(apfs PUBLIC _FILE_OFFSET_BITS=64 _DARWIN_USE_64_BIT_INODE)

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
if (USE_FUSE3)
target_link_libraries(apfs-fuse apfs fuse3)
else()
target_link_libraries(apfs-fuse apfs fuse)
target_compile_definitions(apfs-fuse PRIVATE USE_FUSE2)
endif()
endif()

add_executable(apfsutil ApfsUtil/ApfsUtil.cpp)
target_link_libraries(apfsutil apfs)

include(GNUInstallDirs)
install(TARGETS apfs-fuse RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(TARGETS apfsutil RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

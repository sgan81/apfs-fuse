add_executable(apfs-dump Apfs.cpp)
target_link_libraries(apfs-dump ApfsLib z bz2 lzfse)

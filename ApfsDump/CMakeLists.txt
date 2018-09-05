add_executable(apfs-dump Apfs.cpp Dumper.cpp Dumper.h)
target_link_libraries(apfs-dump ApfsLib z bz2 lzfse)

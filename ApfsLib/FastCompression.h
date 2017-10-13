/*
 * Created..: 31 October 2014
 * Filename.: FastCompression.h
 * Author...: Pike R. Alpha
 * Purpose..: Command line tool to LZVN compress a file.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern size_t lzvn_encode(void * dst, size_t dst_size, const void * src, size_t src_size, void * work_space);
extern size_t lzvn_decode(void * dst, size_t dst_size, const void * src, size_t src_size);
extern size_t lzvn_encode_work_size(void);

#ifdef __cplusplus
}
#endif

/* ZLIB-like interface to fast LZ */

/* extracted from fastlz.h */
#define FASTLZ_VERSION_STRING "0.1.0"

/* must be the last included file */
#ifdef FASTLZ_INCLUDE_CONF_H
#include "conf.h"
#endif
#ifndef ZFASTEXTERN
#define ZFASTEXTERN extern
#endif

/* we are using only zlib types and defines, including z_stream_s */
#define NO_DUMMY_DECL
#include <zlib.h>

/* zfast structure */
typedef z_stream zfast_stream;

/**
 * Return the block size, that is, a size hint which can be used as a lower
 * bound for output buffer allocation and input buffer reads.
 **/
ZFASTEXTERN uInt zfastlibGetBlockSize(void);

/**
 * Return the fastlz library version.
 * (zlib equivalent: zlibVersion)
 **/
ZFASTEXTERN const char * zfastlibVersion(void);

/**
 * Initialize a compressing stream.
 * Returns Z_OK upon success, Z_MEM_ERROR upon memory allocation error.
 * (zlib equivalent: deflateInit)
 **/
ZFASTEXTERN int zfastlibCompressInit(zfast_stream *s, int level);

/**
 * Initialize a decompressing stream.
 * Returns Z_OK upon success, Z_MEM_ERROR upon memory allocation error.
 * (zlib equivalent: inflateInit)
 **/
ZFASTEXTERN int zfastlibDecompressInit(zfast_stream *s);

/**
 * Free allocated data.
 * Returns Z_OK upon success.
 * (zlib equivalent: deflateEnd)
 **/
ZFASTEXTERN int zfastlibCompressEnd(zfast_stream *s);

/**
 * Free allocated data.
 * Returns Z_OK upon success.
 * (zlib equivalent: inflateEnd)
 **/
ZFASTEXTERN int zfastlibDecompressEnd(zfast_stream *s);

/**
 * Reset.
 * Returns Z_OK upon success.
 * (zlib equivalent: deflateReset)
 **/
ZFASTEXTERN int zfastlibCompressReset(zfast_stream *s);

/**
 * Reset.
 * Returns Z_OK upon success.
 * (zlib equivalent: inflateReset)
 **/
ZFASTEXTERN int zfastlibDecompressReset(zfast_stream *s);

/**
 * Return the internal memory buffers size.
 * Returns -1 upon error.
 **/
ZFASTEXTERN int zfastlibCompressMemory(zfast_stream *s);

/**
 * Return the internal memory buffers size.
 * Returns -1 upon error.
 **/
ZFASTEXTERN int zfastlibDecompressMemory(zfast_stream *s);

/**
 * Decompress.
 * (zlib equivalent: inflate)
 **/
ZFASTEXTERN int zfastlibDecompress(zfast_stream *s);

/**
 * Compress.
 * (zlib equivalent: deflate)
 **/
ZFASTEXTERN int zfastlibCompress(zfast_stream *s, int flush);

/**
 * Decompress.
 * (zlib equivalent: inflate)
 * @arg may_buffer if non zero, accept to process partially a stream by using
 * internal buffers. if zero, input data shortage or output buffer room shortage
 * will return Z_BUF_ERROR. in this case, the client should ensure that the
 * input data provided and the output buffer are larger than BUFFER_BLOCK_SIZE
 * before calling again the function. (the output buffer should be validated
 * before getting this code, to ensure that Z_BUF_ERROR implies a need to read
 * additional input data)
 **/
ZFASTEXTERN int zfastlibDecompress2(zfast_stream *const s,
                                    const int may_buffer);

/**
 * Compress.
 * (zlib equivalent: deflate)
 * @arg may_buffer if non zero, accept to process partially a stream by using
 * internal buffers. if zero, input data shortage or output buffer room shortage
 * will return Z_BUF_ERROR. in this case, the client should ensure that the
 * input data provided and the output buffer are larger than BUFFER_BLOCK_SIZE
 * before calling again the function. (the output buffer should be validated
 * before getting this code, to ensure that Z_BUF_ERROR implies a need to read
 * additional input data)
 **/
ZFASTEXTERN int zfastlibCompress2(zfast_stream *const s, int flush,
                                  const int may_buffer);

/* exported internal fats lz lib */

ZFASTEXTERN int fastlz_compress(const void* input, int length, void* output);
ZFASTEXTERN int fastlz_decompress(const void* input, int length, void* output,
                                  int maxout);
ZFASTEXTERN int fastlz_compress_level(int level, const void* input, int length,
                                      void* output);

/* ZLIB-like interface to fast LZ */

/* extracted from fastlz.h */
#define FASTLZ_VERSION_STRING "0.1.0"

/* must be the last included file */
#ifdef FASTLZ_INCLUDE_CONF_H
#include "conf.h"
#endif

/* we are using only zlib types and defines */
#include <zlib.h>

/* zfast structure */
typedef struct zfast_stream_s {
  Bytef    *next_in;  /* next input byte */
  uInt     avail_in;  /* number of bytes available at next_in */
  uLong    total_in;  /* total nb of input bytes read so far */

  Bytef    *next_out; /* next output byte should be put there */
  uInt     avail_out; /* remaining free space at next_out */
  uLong    total_out; /* total nb of bytes output so far */

  char     *msg;      /* last error message, NULL if no error */

  alloc_func zalloc;  /* used to allocate the internal state */
  free_func  zfree;   /* used to free the internal state */
  voidpf     opaque;  /* private data object passed to zalloc and zfree */

  /* private fields */
  
  int level;          /* compression level or 0 for decompressing */

  Bytef inHdr[8];
  uInt inHdrOffs;

  uInt block_type;
  uInt str_size;
  uInt dec_size;
  
  Bytef *inBuff;
  Bytef *outBuff;
  uInt inBuffOffs;
  uInt outBuffOffs;
} zfast_stream_s;

#define ZFAST_LEVEL_BEST_SPEED             1
#define ZFAST_LEVEL_BEST_COMPRESSION       2
#define ZFAST_LEVEL_DEFAULT_COMPRESSION   -1

#define ZFAST_FLUSH_NONE     0
#define ZFAST_FLUSH_SYNC     1
#define ZFAST_FLUSH_FULL     1
#define ZFAST_FLUSH_FINISH   2

#ifndef ZFASTEXTERN
#define ZFASTEXTERN extern
#endif

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
ZFASTEXTERN int zfastlibCompressInit(zfast_stream_s *s, int level);

/**
 * Initialize a decompressing stream.
 * Returns Z_OK upon success, Z_MEM_ERROR upon memory allocation error.
 * (zlib equivalent: inflateInit)
 **/
ZFASTEXTERN int zfastlibDecompressInit(zfast_stream_s *s);

/**
 * Free allocated data.
 * Returns Z_OK upon success.
 * (zlib equivalent: deflateEnd)
 **/
ZFASTEXTERN int zfastlibCompressEnd(zfast_stream_s *s);

/**
 * Free allocated data.
 * Returns Z_OK upon success.
 * (zlib equivalent: inflateEnd)
 **/
ZFASTEXTERN int zfastlibDecompressEnd(zfast_stream_s *s);

/**
 * Reset.
 * Returns Z_OK upon success.
 * (zlib equivalent: deflateReset)
 **/
ZFASTEXTERN int zfastlibCompressReset(zfast_stream_s *s);

/**
 * Reset.
 * Returns Z_OK upon success.
 * (zlib equivalent: inflateReset)
 **/
ZFASTEXTERN int zfastlibDecompressReset(zfast_stream_s *s);

/**
 * Decompress.
 * (zlib equivalent: inflate)
 **/
ZFASTEXTERN int zfastlibDecompress(zfast_stream_s *s);

/**
 * Compress.
 * (zlib equivalent: deflate)
 **/
ZFASTEXTERN int zfastlibCcompress(zfast_stream_s *s, int flush);

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
ZFASTEXTERN int zfastlibDecompress2(zfast_stream_s *const s,
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
ZFASTEXTERN int zfastlibCompress2(zfast_stream_s *const s, int flush,
                                  const int may_buffer);

/* exported internal fats lz lib */

ZFASTEXTERN int fastlz_compress(const void* input, int length, void* output);
ZFASTEXTERN int fastlz_decompress(const void* input, int length, void* output,
                                  int maxout);
ZFASTEXTERN int fastlz_compress_level(int level, const void* input, int length,
                                      void* output);

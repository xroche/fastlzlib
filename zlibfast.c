/* ZLIB-like interface to fast LZ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "zlibfast.h"
#include "fastlz.c"

/* note: the 5% ratio (/20) is not sufficient - add 66 bytes too */
#define EXPANSION_RATIO         10
#define EXPANSION_SECURITY      66
#define HEADER_SIZE             20

#define MIN_BLOCK_SIZE          64
#define DEFAULT_BLOCK_SIZE   32768

/* size of blocks to be compressed */
#define BLOCK_SIZE(S) ( (S)->state->block_size )

/* estimated upper boundary of compressed size */
#define BUFFER_BLOCK_SIZE(S)                                            \
  ( BLOCK_SIZE(S) + BLOCK_SIZE(S) / EXPANSION_RATIO + HEADER_SIZE*2)

/* block types */
#define BLOCK_TYPE_RAW        (0xc0)
#define BLOCK_TYPE_COMPRESSED (0x0c)
#define BLOCK_TYPE_BAD_MAGIC  (0xffff)

/* fake level for decompression */
#define ZFAST_LEVEL_DECOMPRESS (-2)

/* macros */
#define ZFAST_IS_COMPRESSING(S) ( (S)->state->level != ZFAST_LEVEL_DECOMPRESS )
#define ZFAST_IS_DECOMPRESSING(S) ( !ZFAST_IS_COMPRESSING(S) )

/* inlining */
#ifndef ZFASTINLINE
#define ZFASTINLINE
#endif

/* tools */
#define READ_8(adr)  (*(adr))
#define READ_16(adr) ( READ_8(adr) | (READ_8((adr)+1) << 8) )
#define READ_32(adr) ( READ_16(adr) | (READ_16((adr)+2) << 16) )
#define WRITE_8(buff, n) do {                          \
    *((buff))     = (unsigned char) ((n) & 0xff);      \
  } while(0)
#define WRITE_16(buff, n) do {                          \
    *((buff))     = (unsigned char) ((n) & 0xff);       \
    *((buff) + 1) = (unsigned char) ((n) >> 8);         \
  } while(0)
#define WRITE_32(buff, n) do {                  \
    WRITE_16((buff), (n) & 0xffff);             \
    WRITE_16((buff) + 2, (n) >> 16);            \
  } while(0)

/* magic for opaque "state" structure */
static const char MAGIC[8] = {'F', 'a', 's', 't', 'L', 'Z', 0x01, 0};

/* magic for stream (7 bytes with terminating \0) */
static const char* BLOCK_MAGIC = "FastLZ";

/* opaque structure for "state" zlib structure member */
struct internal_state {
  char magic[8];
  
  int level;          /* compression level or 0 for decompressing */

  Bytef inHdr[HEADER_SIZE];
  uInt inHdrOffs;

  uInt block_size;
  uInt block_type;
  uInt str_size;
  uInt dec_size;
  
  Bytef *inBuff;
  Bytef *outBuff;
  uInt inBuffOffs;
  uInt outBuffOffs;
};

/* our typed internal state */
typedef struct internal_state zfast_stream_internal;

/* code version */
const char * zfastlibVersion() {
  return FASTLZ_VERSION_STRING;
}

/* get the preferred minimal block size */
int zfastlibGetBlockSize(zfast_stream *s) {
  if (s != NULL && s->state != NULL) {
    assert(strcmp(s->state->magic, MAGIC) == 0);
    return BLOCK_SIZE(s);
  }
  return 0;
}

int zfastlibGetHeaderSize() {
  return HEADER_SIZE;
}

static voidpf default_zalloc(uInt items, uInt size) {
  return malloc(items*size);
}

static void default_zfree(voidpf address) {
  free(address);
}

static voidpf zalloc(zfast_stream *s, uInt items, uInt size) {
  if (s->zalloc != NULL) {
    return s->zalloc(s->opaque, items, size);
  } else {
    return default_zalloc(items, size);
  }
}

static void zfree(zfast_stream *s, voidpf address) {
  if (s->zfree != NULL) {
    s->zfree(s->opaque, address);
  } else {
    default_zfree(address);
  }
}

/* free private fields */
static void zfastlibFree(zfast_stream *s) {
  if (s != NULL) {
    if (s->state != NULL) {
      assert(strcmp(s->state->magic, MAGIC) == 0);
      if (s->state->inBuff != NULL) {
        zfree(s, s->state->inBuff);
        s->state->inBuff = NULL;
      }
      if (s->state->outBuff != NULL) {
        zfree(s, s->state->outBuff);
        s->state->outBuff = NULL;
      }
      zfree(s, s->state);
      s->state = NULL;
    }
  }
}

static void zfastlibReset(zfast_stream *s) {
  assert(strcmp(s->state->magic, MAGIC) == 0);
  s->msg = NULL;
  s->state->inHdrOffs = 0;
  s->state->block_type = 0;
  s->state->str_size =  0;
  s->state->dec_size =  0;
  s->state->inBuffOffs = 0;
  s->state->outBuffOffs = 0;
}

/* initialize private fields */
static int zfastlibInit(zfast_stream *s, int block_size) {
  if (s != NULL) {
    s->state = (zfast_stream_internal*)
      zalloc(s, sizeof(zfast_stream_internal), 1);
    strcpy(s->state->magic, MAGIC);
    s->state->block_size = (uInt) block_size;
    s->state->inBuff = zalloc(s, BUFFER_BLOCK_SIZE(s), 1);
    s->state->outBuff = zalloc(s, BUFFER_BLOCK_SIZE(s), 1);
    if (s->state->inBuff != NULL && s->state->outBuff != NULL) {
      zfastlibReset(s);
      return Z_OK;
    }
    zfastlibFree(s);
  } else {
    return Z_STREAM_ERROR;
  }
  return Z_MEM_ERROR;
}

int zfastlibCompressInit2(zfast_stream *s, int level, int block_size) {
  const int success = zfastlibInit(s, block_size);
  if (success == Z_OK) {
    /* default or unrecognized compression level */
    if (level < Z_NO_COMPRESSION || level > Z_BEST_COMPRESSION) {
      level = Z_BEST_COMPRESSION;
    }
    s->state->level = level;
  }
  return success;
}

int zfastlibCompressInit(zfast_stream *s, int level) {
  return zfastlibCompressInit2(s, level, DEFAULT_BLOCK_SIZE);
}

int zfastlibDecompressInit2(zfast_stream *s, int block_size) {
  const int success = zfastlibInit(s, DEFAULT_BLOCK_SIZE);
  if (success == Z_OK) {
    s->state->level = ZFAST_LEVEL_DECOMPRESS;
  }
  return success;
}

int zfastlibDecompressInit(zfast_stream *s) {
  return zfastlibDecompressInit2(s, DEFAULT_BLOCK_SIZE);
}

int zfastlibCompressEnd(zfast_stream *s) {
  if (s == NULL) {
    return Z_STREAM_ERROR;
  }
  zfastlibFree(s);
  return Z_OK;
}

int zfastlibDecompressEnd(zfast_stream *s) {
  return zfastlibCompressEnd(s);
}

int zfastlibCompressReset(zfast_stream *s) {
  if (s == NULL) {
    return Z_STREAM_ERROR;
  }
  zfastlibReset(s);
  return Z_OK;
}

int zfastlibDecompressReset(zfast_stream *s) {
  return zfastlibCompressReset(s);
}

int zfastlibCompressMemory(zfast_stream *s) {
  if (s == NULL || s->state == NULL) {
    return -1;
  }
  return (int) ( sizeof(zfast_stream_internal) + BUFFER_BLOCK_SIZE(s) * 2 );
}

int zfastlibDecompressMemory(zfast_stream *s) {
  return zfastlibCompressMemory(s);
}

static ZFASTINLINE void inSeek(zfast_stream *s, uInt offs) {
  assert(s->avail_in >= offs);
  s->next_in += offs;
  s->avail_in -= offs;
  s->total_in += offs;
}

static ZFASTINLINE void outSeek(zfast_stream *s, uInt offs) {
  assert(s->avail_out >= offs);
  s->next_out += offs;
  s->avail_out -= offs;
  s->total_out += offs;
}

static ZFASTINLINE int zlibLevelToFastlz(int level) {
  return level <= Z_BEST_SPEED ? 1 : 2;
}

static ZFASTINLINE int fastlz_write_header(Bytef* dest,
                                           uInt type,
                                           uInt block_size,
                                           uInt compressed,
                                           uInt original) {
  memcpy(&dest[0], BLOCK_MAGIC, 7);
  WRITE_8(&dest[7], type);
  WRITE_32(&dest[8], compressed);
  WRITE_32(&dest[12], original);
  WRITE_32(&dest[16], block_size);
  return HEADER_SIZE;
}

static ZFASTINLINE void fastlz_read_header(const Bytef* source,
                                           uInt *type,
                                           uInt *block_size,
                                           uInt *compressed,
                                           uInt *original) {
  if (memcmp(&source[0], BLOCK_MAGIC, 7) == 0) {
    *type = READ_8(&source[7]);
    *compressed = READ_32(&source[8]);
    *original = READ_32(&source[12]);
    *block_size = READ_32(&source[12]);
  } else {
    *type = BLOCK_TYPE_BAD_MAGIC;
    *compressed =  0;
    *original =  0;
    *block_size = 0;
  }
}

int zfastlibGetStreamBlockSize(const void* input, int length) {
  uInt block_size = 0;
  if (length >= HEADER_SIZE) {
    uInt block_type;
    uInt str_size;
    uInt dec_size;
    fastlz_read_header((const Bytef*) input, &block_type, &block_size,
                       &str_size, &dec_size);
  }
  return block_size;
}

static ZFASTINLINE int fastlz_compress_hdr(const void* input, uInt length,
                                           void* output, uInt output_length,
                                           int block_size, int level,
                                           int flush) {
  uInt done = 0;
  Bytef*const output_start = (Bytef*) output;
  if (length > 0) {
    void*const output_data_start = &output_start[HEADER_SIZE];
    uInt type;
    /* compress and fill header after */
    if (length > MIN_BLOCK_SIZE) {
      done = fastlz_compress_level(level, input, length, output_data_start);
      assert(done + HEADER_SIZE*2 <= output_length);
      type = BLOCK_TYPE_COMPRESSED;
    } else {
      assert(length + HEADER_SIZE*2 <= output_length);
      memcpy(output_data_start, input, length);
      done = length;
      type = BLOCK_TYPE_RAW;
    }
    /* write back header */
    done += fastlz_write_header(output_start, type, block_size, done, length);
  }
  /* write an EOF marker (empty block with compressed=uncompressed=0) */
  if (flush == Z_FINISH) {
    Bytef*const output_end = &output_start[done];
    done += fastlz_write_header(output_end, BLOCK_TYPE_COMPRESSED, block_size,
                                0, 0);
  }
  assert(done <= output_length);
  return done;
}

/*
 * Compression and decompression processing routine.
 * The only difference with compression is that the input and output are
 * variables (may change with flush)
 */
static ZFASTINLINE int zfastlibProcess(zfast_stream *const s, const int flush,
                                       const int may_buffer) {
  const Bytef *in = NULL;

  /* sanity check for next_in/next_out */
  if (s->avail_in != 0 && s->next_in == NULL) {
    s->msg = "Invalid input";
    return Z_STREAM_ERROR;
  }
  else if (s->avail_out != 0 && s->next_out == NULL) {
    s->msg = "Invalid output";
    return Z_STREAM_ERROR;
  }
  
  /* output buffer data to be processed */
  if (s->state->outBuffOffs < s->state->dec_size) {
    /* maximum size that can be copied */
    uInt size = s->state->dec_size - s->state->outBuffOffs;
    if (size > s->avail_out) {
      size = s->avail_out;
    }
    /* copy and seek */
    if (size > 0) {
      memcpy(s->next_out, &s->state->outBuff[s->state->outBuffOffs], size);
      s->state->outBuffOffs += size;
      outSeek(s, size);
    }
    /* and return chunk */
    return Z_OK;
  }

  /* read next block (note: output buffer is empty here) */
  else if (s->state->str_size == 0) {
    /* for error reporting only */
    uInt block_size = 0;

    /* decompressing: header is present */
    if (ZFAST_IS_DECOMPRESSING(s)) {
      /* if header read in progress or will be in multiple passes (sheesh) */
      if (s->state->inHdrOffs != 0 || s->avail_in < HEADER_SIZE) {
        /* we are to go buffered for the header - check if this is allowed */
        if (s->state->inHdrOffs == 0 && !may_buffer) {
          s->msg = "Need more data on input";
          return Z_BUF_ERROR;
        }
        /* copy up to HEADER_SIZE bytes */
        for( ; s->avail_in > 0 && s->state->inHdrOffs < HEADER_SIZE
               ; s->state->inHdrOffs++, inSeek(s, 1)) {
          s->state->inHdr[s->state->inHdrOffs] = *s->next_in;
        }
      }
      /* process header if completed */

      /* header on client region */
      if (s->state->inHdrOffs == 0 && s->avail_in >= HEADER_SIZE) {
        /* peek header */
        uInt block_type;
        uInt str_size;
        uInt dec_size;
        fastlz_read_header(s->next_in, &block_type, &block_size,
                           &str_size, &dec_size);

        /* not buffered: check if we can do the job at once */
        if (!may_buffer) {
          /* input buffer too small */
          if (s->avail_in < str_size) {
            s->msg = "Need more data on input";
            return Z_BUF_ERROR;
          }
          /* output buffer too small */
          else if (s->avail_out < dec_size) {
            s->msg = "Need more room on output";
            return Z_BUF_ERROR;
          }
        }

        /* apply/eat the header and continue */
        s->state->block_type = block_type;
        s->state->str_size =  str_size;
        s->state->dec_size =  dec_size;
        inSeek(s, HEADER_SIZE);
      }
      /* header in inHdrOffs buffer */
      else if (s->state->inHdrOffs == HEADER_SIZE) {
        /* peek header */
        uInt block_type;
        uInt str_size;
        uInt dec_size;
        assert(may_buffer);  /* impossible at this point */
        fastlz_read_header(s->state->inHdr, &block_type, &block_size,
                           &str_size, &dec_size);
        s->state->block_type = block_type;
        s->state->str_size = str_size;
        s->state->dec_size = dec_size;
        s->state->inHdrOffs = 0;
      }
      /* otherwise, please comd back later (header not fully processed) */
      else {
        assert(may_buffer);  /* impossible at this point */
        return Z_OK;
      }
    }
    /* decompressing: fixed input size (unless flush) */
    else {
      uInt block_type = BLOCK_TYPE_COMPRESSED;
      uInt str_size = BLOCK_SIZE(s);

      /* not enough room on input */
      if (str_size > s->avail_in) {
        if (flush > Z_NO_FLUSH) {
          str_size = s->avail_in;
        } else if (!may_buffer) {
          s->msg = "Need more data on input";
          return Z_BUF_ERROR;
        }
      }
      
      /* apply/eat the header and continue */
      s->state->block_type = block_type;
      s->state->str_size =  str_size;
      s->state->dec_size =  0;  /* yet unknown */
    }
    
    /* sanity check */
    if (s->state->block_type == BLOCK_TYPE_BAD_MAGIC) {
      s->msg = "Corrupted compressed stream (bad magic)";
      return Z_DATA_ERROR;
    }
    else if (s->state->block_type != BLOCK_TYPE_RAW
             && s->state->block_type != BLOCK_TYPE_COMPRESSED) {
      s->msg = "Corrupted compressed stream (illegal block type)";
      return Z_VERSION_ERROR;
    }
    else if (block_size > BLOCK_SIZE(s)) {
      s->msg = "Block size too large";
      return Z_VERSION_ERROR;
    }
    else if (s->state->dec_size > BUFFER_BLOCK_SIZE(s)) {
      s->msg = "Corrupted compressed stream (illegal decompressed size)";
      return Z_VERSION_ERROR;
    }
    else if (s->state->str_size > BUFFER_BLOCK_SIZE(s)) {
      s->msg = "Corrupted compressed stream (illegal stream size)";
      return Z_VERSION_ERROR;
    }
   
    /* output not buffered yet */
    s->state->outBuffOffs = s->state->dec_size;

    /* compressed and uncompressed == 0 : EOF marker */
    if (s->state->str_size == 0 && s->state->dec_size == 0) {
      return Z_STREAM_END;
    }

    /* direct data fully available (ie. complete compressed block) ? */
    if (s->avail_in >= s->state->str_size) {
      in = s->next_in;
      inSeek(s, s->state->str_size);
    }
    /* otherwise, buffered */
    else {
      s->state->inBuffOffs = 0;
    }
  }

  /* notes: */
  /* - header always processed at this point */
  /* - no output buffer data to be processed (outBuffOffs == 0) */
 
  /* bufferred data: copy as much as possible to inBuff until we have the
     block data size */
  if (in == NULL) {
    /* remaining data to copy in input buffer */
    if (s->state->inBuffOffs < s->state->str_size) {
      uInt size = s->state->str_size - s->state->inBuffOffs;
      if (size > s->avail_in) {
        size = s->avail_in;
      }
      if (size > 0) {
        memcpy(&s->state->inBuff[s->state->inBuffOffs], s->next_in, size);
        s->state->inBuffOffs += size;
        inSeek(s, size);
      }
    }
    /* block stream size (ie. compressed one) reached */
    if (s->state->inBuffOffs == s->state->str_size) {
      in = s->state->inBuff;
    }
    /* forced flush: adjust str_size */
    else if (flush != Z_NO_FLUSH) {
      in = s->state->inBuff;
      s->state->str_size = s->state->inBuffOffs;
    }
  }

  /* we have a complete compressed block (str_size) : where to uncompress ? */
  if (in != NULL) {
    Bytef *out = NULL;
    const uInt in_size = s->state->str_size;

    /* we are supposed to finish, but we did not eat all data: ignore for now */
    int flush_now = flush;
    if (flush_now == Z_FINISH && s->avail_in != 0) {
      flush_now = Z_NO_FLUSH;
    }
      
    /* decompressing */
    if (ZFAST_IS_DECOMPRESSING(s)) {
      int done;
      const uInt out_size = s->state->dec_size;

      /* can decompress directly on client memory */
      if (s->avail_out >= s->state->dec_size) {
        out = s->next_out;
        outSeek(s, s->state->dec_size);
        /* no buffer */
        s->state->outBuffOffs = s->state->dec_size;
      }
      /* otherwise in output buffer */
      else {
        out = s->state->outBuff;
        s->state->outBuffOffs = 0;
      }

      /* input eaten */
      s->state->str_size = 0;

      /* rock'in */
      done = fastlz_decompress(in, in_size, out, out_size);
      if (done != (int) s->state->dec_size) {
        s->msg = "Unable to decompress block stream";
        return Z_STREAM_ERROR;
      }
    }
    /* compressing */
    else {
      /* note: if < MIN_BLOCK_SIZE, fastlz_compress_hdr will not compress */
      const uInt estimated_dec_size = in_size + in_size / EXPANSION_RATIO
        + EXPANSION_SECURITY;

      /* can compress directly on client memory */
      if (s->avail_out >= estimated_dec_size) {
        const int done = fastlz_compress_hdr(in, in_size,
                                             s->next_out, estimated_dec_size,
                                             BLOCK_SIZE(s),
                                             zlibLevelToFastlz(s->state->level),
                                             flush_now);
        /* seek output */
        outSeek(s, done);
        /* no buffer */
        s->state->outBuffOffs = s->state->dec_size;
      }
      /* otherwise in output buffer */
      else {
        const int done = fastlz_compress_hdr(in, in_size,
                                             s->state->outBuff,
                                             BUFFER_BLOCK_SIZE(s),
                                             BLOCK_SIZE(s),
                                             zlibLevelToFastlz(s->state->level),
                                             flush_now);
        /* produced size (in outBuff) */
        s->state->dec_size = (uInt) done;
        /* bufferred */
        s->state->outBuffOffs = 0;
      }

      /* input eaten */
      s->state->str_size = 0;
    }
  }

  /* so far so good */
  
  return Z_OK;
}

int zfastlibDecompress2(zfast_stream *s, const int may_buffer) {
  if (ZFAST_IS_DECOMPRESSING(s)) {
    return zfastlibProcess(s, Z_NO_FLUSH, may_buffer);
  } else {
    s->msg = "Decompressing function used with a compressing stream";
    return Z_STREAM_ERROR;
  }
}

int zfastlibDecompress(zfast_stream *s) {
  return zfastlibDecompress2(s, 1);
}

int zfastlibCompress2(zfast_stream *s, int flush, const int may_buffer) {
  if (ZFAST_IS_COMPRESSING(s)) {
    return zfastlibProcess(s, flush, may_buffer);
  } else {
    s->msg = "Compressing function used with a decompressing stream";
    return Z_STREAM_ERROR;
  }
}

int zfastlibCompress(zfast_stream *s, int flush) {
  return zfastlibCompress2(s, flush, 1);
}

int zfastlibIsCompressedStream(const void* input, int length) {
  if (length >= HEADER_SIZE) {
    const Bytef*const in = (const Bytef*) input;
    return zfastlibGetStreamBlockSize(in, length) != 0 ? Z_OK : Z_DATA_ERROR;
  } else {
    return Z_BUF_ERROR;
  }
}

int zfastlibDecompressSync(zfast_stream *s) {
  if (ZFAST_IS_DECOMPRESSING(s)) {
    if (s->state->outBuffOffs < s->state->dec_size) {
      /* not in an error state: uncompressed data available in buffer */
      return Z_OK;
    }
    else {
      /* Note: if s->state->str_size == 0, we are not in an error state: the
         next chunk is to be read; However, we check the chunk anyway. */

      /* at least HEADER_SIZE data */
      if (s->avail_in < HEADER_SIZE) {
        s->msg = "Need more data on input";
        return Z_BUF_ERROR;
      }
        
      /* in buffered read of the header.. reset to 0 */
      if (s->state->inHdrOffs != 0) {
        s->state->inHdrOffs = 0;
      }
      
      /* seek */
      for( ; s->avail_in >= HEADER_SIZE
             ; s->state->inHdrOffs++, inSeek(s, 1)) {
        const Bytef *const in = s->next_in;
        if (in[0] == BLOCK_MAGIC[0]
            && in[1] == BLOCK_MAGIC[1]
            && in[2] == BLOCK_MAGIC[2]
            && in[3] == BLOCK_MAGIC[3]
            && in[4] == BLOCK_MAGIC[4]
            && in[5] == BLOCK_MAGIC[5]
            && in[6] == BLOCK_MAGIC[6]
            ) {
          const int block_size = zfastlibGetStreamBlockSize(in, HEADER_SIZE);
          if (block_size != 0) {
            /* successful seek */
            return Z_OK;
          }
        }
      }
      s->msg = "No flush point found";
      return Z_DATA_ERROR;
    }
  } else {
    s->msg = "Decompressing function used with a compressing stream";
    return Z_STREAM_ERROR;
  }
}

/* ZLIB-like interface to fast LZ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "zlibfast.h"
#include "fastlz.c"

#define MIN_BLOCK_SIZE    64
#define HEADER_SIZE        9
#define MAX_BLOCK_SIZE 32768
#define BUFFER_BLOCK_SIZE \
  ( MAX_BLOCK_SIZE + MAX_BLOCK_SIZE / 10 + HEADER_SIZE*2)

#define BLOCK_TYPE_RAW        0xc0
#define BLOCK_TYPE_COMPRESSED 0x0c

/* fake level for decompression */
#define ZFAST_LEVEL_DECOMPRESS (-2)
#define ZFAST_IS_COMPRESSING(S) ( (S)->state->level != ZFAST_LEVEL_DECOMPRESS )

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

static const char MAGIC[8] = {'F', 'a', 's', 't', 'L', 'Z', 0x01, 0};

/* opaque structure for "state" zlib structure member */
struct internal_state {
  char magic[8];
  
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
};

/* our typed internal state */
typedef struct internal_state zfast_stream_internal;

/* code version */
const char * zfastlibVersion() {
  return FASTLZ_VERSION_STRING;
}

/* get the preferred minimal block size */
uInt zfastlibGetBlockSize() {
  return HEADER_SIZE + BUFFER_BLOCK_SIZE;
}

static voidpf default_zalloc(voidpf opaque, uInt items, uInt size) {
  return malloc(items*size);
}

static void default_zfree(voidpf opaque, voidpf address) {
  free(address);
}

/* free private fields */
static void zfastlibFree(zfast_stream *s) {
  if (s != NULL) {
    if (s->state != NULL) {
      assert(strcmp(s->state->magic, MAGIC) == 0);
      if (s->state->inBuff != NULL) {
        s->zfree(s->opaque, s->state->inBuff);
        s->state->inBuff = NULL;
      }
      if (s->state->outBuff != NULL) {
        s->zfree(s->opaque, s->state->outBuff);
        s->state->outBuff = NULL;
      }
      s->zfree(s->opaque, s->state);
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
static int zfastlibInit(zfast_stream *s) {
  if (s != NULL) {
    if (s->zalloc == NULL) {
      s->zalloc = default_zalloc;
    }
    if (s->zfree == NULL) {
      s->zfree = default_zfree;
    }

    s->state = s->zalloc(s->opaque, sizeof(zfast_stream_internal), 1);
    strcpy(s->state->magic, MAGIC);
    s->state->inBuff = s->zalloc(s->opaque, BUFFER_BLOCK_SIZE, 1);
    s->state->outBuff = s->zalloc(s->opaque, BUFFER_BLOCK_SIZE, 1);
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

int zfastlibCompressInit(zfast_stream *s, int level) {
  const int success = zfastlibInit(s);
  if (success == Z_OK) {
    /* default or unrecognized compression level */
    if (level < Z_NO_COMPRESSION || level > Z_BEST_COMPRESSION) {
      level = Z_BEST_COMPRESSION;
    }
    s->state->level = level;
  }
  return success;
}

int zfastlibDecompressInit(zfast_stream *s) {
  const int success = zfastlibInit(s);
  if (success == Z_OK) {
    s->state->level = ZFAST_LEVEL_DECOMPRESS;
  }
  return success;
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
  if (s == NULL || s->opaque == NULL) {
    return -1;
  }
  return (int) ( sizeof(zfast_stream_internal) + BUFFER_BLOCK_SIZE * 2 );
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

static ZFASTINLINE int fastlz_compress_hdr(const void* input, int length,
                                           void* output, int output_length,
                                           int level, int flush) {
  Bytef*const output_start = (Bytef*) output;
  void*const output_data_start = &output_start[HEADER_SIZE];
  int done;
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
  WRITE_8(output_start, type);
  WRITE_32(&output_start[1], done);
  WRITE_32(&output_start[5], length);
  done += HEADER_SIZE;
  /* write an EOF marker (empty block with compressed=uncompressed=0) */
  if (flush == Z_FINISH) {
    Bytef*const output_end = &output_start[done];
    WRITE_8(output_end, BLOCK_TYPE_COMPRESSED);
    WRITE_32(&output_start[1], 0);
    WRITE_32(&output_start[5], 0);
    done += HEADER_SIZE;
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

    /* decompressing: header is present */
    if (ZFAST_IS_COMPRESSING(s)) {
      /* if header read in progress or will be in multiple passes (sheesh) */
      if (s->state->inHdrOffs != 0 || s->avail_in < HEADER_SIZE) {
        uInt i;
        /* we are to go buffered for the header - check if this is allowed */
        if (s->state->inHdrOffs == 0 && !may_buffer) {
          s->msg = "Need more data on input";
          return Z_BUF_ERROR;
        }
        /* copy up to HEADER_SIZE bytes */
        for(i = s->state->inHdrOffs ; s->avail_in > 0 && i < HEADER_SIZE
              ; i++) {
          s->state->inHdr[i] = *s->next_in;
          inSeek(s, 1);
        }
      }
      /* process header if completed */

      /* header on client region */
      if (s->state->inHdrOffs == 0 && s->avail_in >= HEADER_SIZE) {
        /* peek header */
        const uInt block_type = READ_8(&s->next_in[0]);
        const uInt str_size =  READ_32(&s->next_in[1]);
        const uInt dec_size =  READ_32(&s->next_in[5]);

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
        assert(may_buffer);  /* impossible at this point */
        s->state->block_type = READ_8(&s->next_in[0]);
        s->state->str_size =  READ_32(&s->state->inHdr[1]);
        s->state->dec_size =  READ_32(&s->state->inHdr[5]);
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
      uInt str_size = MAX_BLOCK_SIZE;

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
    if (s->state->block_type != BLOCK_TYPE_RAW
        && s->state->block_type != BLOCK_TYPE_COMPRESSED) {
      s->msg = "Corrupted compressed stream (illegal block type)";
      return Z_STREAM_ERROR;
    }
    else if (s->state->dec_size > BUFFER_BLOCK_SIZE) {
      s->msg = "Corrupted compressed stream (illegal decompressed size)";
      return Z_STREAM_ERROR;
    }
    else if (s->state->str_size > BUFFER_BLOCK_SIZE) {
      s->msg = "Corrupted compressed stream (illegal stream size)";
      return Z_STREAM_ERROR;
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
    
    /* decompressing */
    if (ZFAST_IS_COMPRESSING(s)) {
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
      const uInt estimated_dec_size = in_size + in_size / 10;

      /* can compress directly on client memory */
      if (s->avail_out >= estimated_dec_size) {
        const int done = fastlz_compress_hdr(in, in_size,
                                             s->next_out, estimated_dec_size,
                                             zlibLevelToFastlz(s->state->level),
                                             flush);
        /* seek output */
        outSeek(s, done);
        /* no buffer */
        s->state->outBuffOffs = s->state->dec_size;
      }
      /* otherwise in output buffer */
      else {
        const int done = fastlz_compress_hdr(in, in_size,
                                             s->state->outBuff,
                                             BUFFER_BLOCK_SIZE,
                                             zlibLevelToFastlz(s->state->level),
                                             flush);
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

int zfastlibDecompress2(zfast_stream *const s, const int may_buffer) {
  if (!ZFAST_IS_COMPRESSING(s)) {
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

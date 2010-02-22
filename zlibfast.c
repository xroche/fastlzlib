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
#define ZFAST_LEVEL_DECOMPRESS -2
#define ZFAST_IS_COMPRESSING(S) ( (S)->level != ZFAST_LEVEL_DECOMPRESS )

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
static void zfastlibFree(zfast_stream_s *s) {
  if (s->inBuff != NULL) {
    s->zfree(s->opaque, s->inBuff);
    s->inBuff = NULL;
  }
  if (s->outBuff != NULL) {
    s->zfree(s->opaque, s->outBuff);
    s->outBuff = NULL;
  }
}

/* initialize private fields */
static int zfastlibInit(zfast_stream_s *s) {
  if (s->zalloc == NULL) {
    s->zalloc = default_zalloc;
  }
  if (s->zfree == NULL) {
    s->zfree = default_zfree;
  }
  s->inBuff = s->zalloc(s->opaque, BUFFER_BLOCK_SIZE, 1);
  s->outBuff = s->zalloc(s->opaque, BUFFER_BLOCK_SIZE, 1);
  s->msg = NULL;
  s->inBuffOffs = 0;
  s->outBuffOffs = 0;
  s->inHdrOffs = 0;
  s->str_size =  0;
  s->dec_size =  0;
  if (s->inBuff != NULL && s->outBuff != NULL) {
    return Z_OK;
  }
  zfastlibFree(s);
  return Z_MEM_ERROR;
}

int zfastlibCompressInit(zfast_stream_s *s, int level) {
  if (s == NULL) {
    return Z_STREAM_ERROR;
  }
  /* default or unrecognized compression level */
  if (level != ZFAST_LEVEL_BEST_SPEED
      && level != ZFAST_LEVEL_BEST_COMPRESSION) {
    level = ZFAST_LEVEL_BEST_COMPRESSION;
  }
  s->level = level;
  return zfastlibInit(s);
}

int zfastlibDecompressInit(zfast_stream_s *s) {
  if (s == NULL) {
    return Z_STREAM_ERROR;
  }
  s->level = ZFAST_LEVEL_DECOMPRESS;
  return zfastlibInit(s);
}

int zfastlibCompressEnd(zfast_stream_s *s) {
  if (s == NULL) {
    return Z_STREAM_ERROR;
  }
  zfastlibFree(s);
  return Z_OK;
}

int zfastlibDecompressEnd(zfast_stream_s *s) {
  if (s == NULL) {
    return Z_STREAM_ERROR;
  }
  zfastlibFree(s);
  return Z_OK;
}

static ZFASTINLINE void inSeek(zfast_stream_s *s, uInt offs) {
  assert(s->avail_in >= offs);
  s->next_in += offs;
  s->avail_in -= offs;
  s->total_in += offs;
}

static ZFASTINLINE void outSeek(zfast_stream_s *s, uInt offs) {
  assert(s->avail_out >= offs);
  s->next_out += offs;
  s->avail_out -= offs;
  s->total_out += offs;
}

static ZFASTINLINE int fastlz_compress_hdr(const void* input, int length,
                                           void* output, int output_length,
                                           int flush) {
  Bytef*const output_start = (Bytef*) output;
  void*const output_data_start = &output_start[HEADER_SIZE];
  int done;
  uInt type;
  /* compress and fill header after */
  if (length > MIN_BLOCK_SIZE) {
    done = fastlz_compress(input, length, output_data_start);
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
  if (flush == ZFAST_FLUSH_FINISH) {
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
static ZFASTINLINE int zfastlibProcess(zfast_stream_s *const s, const int flush,
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
  if (s->outBuffOffs < s->dec_size) {
    /* maximum size that can be copied */
    uInt size = s->dec_size - s->outBuffOffs;
    if (size > s->avail_out) {
      size = s->avail_out;
    }
    /* copy and seek */
    if (size > 0) {
      memcpy(s->next_out, &s->outBuff[s->outBuffOffs], size);
      s->outBuffOffs += size;
      outSeek(s, size);
    }
    /* and return chunk */
    return Z_OK;
  }

  /* read next block (note: output buffer is empty here) */
  else if (s->str_size == 0) {

    /* decompressing: header is present */
    if (ZFAST_IS_COMPRESSING(s)) {
      /* if header read in progress or will be in multiple passes (sheesh) */
      if (s->inHdrOffs != 0 || s->avail_in < HEADER_SIZE) {
        uInt i;
        /* we are to go buffered for the header - check if this is allowed */
        if (s->inHdrOffs == 0 && !may_buffer) {
          s->msg = "Need more data on input";
          return Z_BUF_ERROR;
        }
        /* copy up to HEADER_SIZE bytes */
        for(i = s->inHdrOffs ; s->avail_in > 0 && i < HEADER_SIZE ; i++) {
          s->inHdr[i] = *s->next_in;
          inSeek(s, 1);
        }
      }
      /* process header if completed */

      /* header on client region */
      if (s->inHdrOffs == 0 && s->avail_in >= HEADER_SIZE) {
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
        s->block_type = block_type;
        s->str_size =  str_size;
        s->dec_size =  dec_size;
        inSeek(s, HEADER_SIZE);
      }
      /* header in inHdrOffs buffer */
      else if (s->inHdrOffs == HEADER_SIZE) {
        assert(may_buffer);  /* impossible at this point */
        s->block_type = READ_8(&s->next_in[0]);
        s->str_size =  READ_32(&s->inHdr[1]);
        s->dec_size =  READ_32(&s->inHdr[5]);
        s->inHdrOffs = 0;
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
        str_size = s->avail_in;
        if (flush == ZFAST_FLUSH_SYNC) {
        } else if (!may_buffer) {
          s->msg = "Need more data on input";
          return Z_BUF_ERROR;
        }
      }
      
      /* apply/eat the header and continue */
      s->block_type = block_type;
      s->str_size =  str_size;
      s->dec_size =  0;  /* yet unknown */
    }
    
    /* sanity check */
    if (s->block_type != BLOCK_TYPE_RAW
        && s->block_type != BLOCK_TYPE_COMPRESSED) {
      s->msg = "Corrupted compressed stream (illegal block type)";
      return Z_STREAM_ERROR;
    }
    else if (s->dec_size > BUFFER_BLOCK_SIZE) {
      s->msg = "Corrupted compressed stream (illegal decompressed size)";
      return Z_STREAM_ERROR;
    }
    else if (s->str_size > BUFFER_BLOCK_SIZE) {
      s->msg = "Corrupted compressed stream (illegal stream size)";
      return Z_STREAM_ERROR;
    }
    
    /* output not buffered yet */
    s->outBuffOffs = s->dec_size;

    /* compressed and uncompressed == 0 : EOF marker */
    if (s->str_size == 0 && s->dec_size == 0) {
      return Z_STREAM_END;
    }

    /* direct data fully available (ie. complete compressed block) ? */
    if (s->avail_in >= s->str_size) {
      in = s->next_in;
      inSeek(s, s->str_size);
    }
    /* otherwise, buffered */
    else {
      s->inBuffOffs = 0;
    }
  }

  /* notes: */
  /* - header always processed at this point */
  /* - no output buffer data to be processed (outBuffOffs == 0) */
 
  /* bufferred data: copy as much as possible to inBuff until we have the
     block data size */
  if (in == NULL) {
    /* remaining data to copy in input buffer */
    if (s->inBuffOffs < s->str_size) {
      uInt size = s->str_size - s->inBuffOffs;
      if (size > s->avail_in) {
        size = s->avail_in;
      }
      if (size > 0) {
        memcpy(&s->inBuff[s->inBuffOffs], s->next_in, size);
        s->inBuffOffs += size;
        inSeek(s, size);
      }
    }
    /* block stream size (ie. compressed one) reached */
    if (s->inBuffOffs == s->str_size) {
      in = s->inBuff;
    }
    /* forced flush: adjust str_size */
    else if (flush != ZFAST_FLUSH_NONE) {
      in = s->inBuff;
      s->str_size = s->inBuffOffs;
    }
  }

  /* we have a complete compressed block (str_size) : where to uncompress ? */
  if (in != NULL) {
    Bytef *out = NULL;
    const uInt in_size = s->str_size;
    
    /* decompressing */
    if (ZFAST_IS_COMPRESSING(s)) {
      int done;
      const uInt out_size = s->dec_size;

      /* can decompress directly on client memory */
      if (s->avail_out >= s->dec_size) {
        out = s->next_out;
        outSeek(s, s->dec_size);
        /* no buffer */
        s->outBuffOffs = s->dec_size;
      }
      /* otherwise in output buffer */
      else {
        out = s->outBuff;
        s->outBuffOffs = 0;
      }

      /* input eaten */
      s->str_size = 0;

      /* rock'in */
      done = fastlz_decompress(in, in_size, out, out_size);
      if (done != (int) s->dec_size) {
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
                                             flush);
        /* seek output */
        outSeek(s, done);
        /* no buffer */
        s->outBuffOffs = s->dec_size;
      }
      /* otherwise in output buffer */
      else {
        const int done = fastlz_compress_hdr(in, in_size,
                                             s->outBuff, BUFFER_BLOCK_SIZE,
                                             flush);
        /* produced size (in outBuff) */
        s->dec_size = (uInt) done;
        /* bufferred */
        s->outBuffOffs = 0;
      }

      /* input eaten */
      s->str_size = 0;
    }
  }

  /* so far so good */
  
  return Z_OK;
}

int zfastlibDecompress2(zfast_stream_s *const s, const int may_buffer) {
  if (!ZFAST_IS_COMPRESSING(s)) {
    return zfastlibProcess(s, ZFAST_FLUSH_NONE, may_buffer);
  } else {
    s->msg = "Decompressing function used with a compressing stream";
    return Z_STREAM_ERROR;
  }
}

int zfastlibDecompress(zfast_stream_s *s) {
  return zfastlibDecompress2(s, 1);
}

int zfastlibCompress2(zfast_stream_s *s, int flush, const int may_buffer) {
  if (ZFAST_IS_COMPRESSING(s)) {
    return zfastlibProcess(s, flush, may_buffer);
  } else {
    s->msg = "Compressing function used with a decompressing stream";
    return Z_STREAM_ERROR;
  }
}

int zfastlibCompress(zfast_stream_s *s, int flush) {
  return zfastlibCompress2(s, flush, 1);
}

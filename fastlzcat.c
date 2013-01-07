/*  
  zlib-like interface to fast block compression (LZ4 or FastLZ) libraries
  Copyright (C) 2010-2013 Exalead SA. (http://www.exalead.com/)
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

  Remarks/Bugs:
  LZ4 compression library by Yann Collet (yann.collet.73@gmail.com)
  FastLZ compression library by Ariya Hidayat (ariya@kde.org)
  Library encapsulation by Xavier Roche (fastlz@exalead.com)
*/

/* compress or uncompress streams */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "fastlzlib.h"

static void usage(char *arg0) {
  fprintf(stderr,
          "%s, FastLZ compression/decompression tool.\n"
          "Usage: %s (filename|-) (filename ..)\t#input filename(s) or stdin\n"
          "\t[--output (filename|-)]\t#output filename or stdout\n"
          "\t[--compress|--decompress]\t#mode\n"
          "\t[--lz4|--fastlz]\t#compression type\n"
          "\t[--fast|--normal]\t#compression speed\n"
          "\t[--inbufsize n]\t#input buffer size (262144)\n"
          "\t[--outbufsize n]\t#output buffer size (1048576)\n"
          "\t[--blocksize n]\t#block stream size (1048576)\n"
          "\t[--flush]\t#flush uncompressed data regularly\n"
          ,
          arg0, arg0);
}

static void error(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}

static void syserror(const char *msg) {
  const int e = errno;
  fprintf(stderr, "%s: %s\n", msg, strerror(e));
  exit(EXIT_FAILURE);
}

static void flzerror(zfast_stream *s, const char *msg) {
  fprintf(stderr, "%s: %s\n", msg, s->msg != NULL ? s->msg : "unknown error");
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  int *files = malloc(sizeof(int) * argc);
  int nfiles = 0;
  const char *output = NULL;
  int compress = 0;
  int list = 0;
  int flush = 0;
  zfast_stream_compressor type = COMPRESSOR_FASTLZ;
  int perfs = 2;
  uInt block_size = 262144;
  uInt inbufsize = 1048576;
  uInt outbufsize = 1048576;
  int i;

  /* process args */
  for(i = 1 ; i < argc ; i++) {
    if (strcmp(argv[i], "--compress") == 0) {
      compress = 1;
    }
    else if (strcmp(argv[i], "--decompress") == 0
             || strcmp(argv[i], "--uncompress") == 0
             || strcmp(argv[i], "-d") == 0) {
      compress = 0;
    }
    else if (strcmp(argv[i], "--list") == 0
             || strcmp(argv[i], "-l") == 0) {
      list = 1;
    }
    else if (strcmp(argv[i], "--flush") == 0) {
      flush = 1;
    }
    else if (strcmp(argv[i], "--lz4") == 0) {
      type = COMPRESSOR_LZ4;
    }
    else if (strcmp(argv[i], "--fastlz") == 0) {
      type = COMPRESSOR_FASTLZ;
    }
    else if (strcmp(argv[i], "--fast") == 0) {
      perfs = 1;
    }
    else if (strcmp(argv[i], "--normal") == 0) {
      perfs = 2;
    }
    else if (i + 1 < argc && strcmp(argv[i], "--inbufsize") == 0) {
      int size;
      if (sscanf(argv[i + 1], "%d", &size) == 1) {
        inbufsize = size;
      } else {
        error("invalid size");
      }
      i++;
    }
    else if (i + 1 < argc && strcmp(argv[i], "--outbufsize") == 0) {
      int size;
      if (sscanf(argv[i + 1], "%d", &size) == 1) {
        outbufsize = size;
      } else {
        error("invalid size");
      }
      i++;
    }
    else if (i + 1 < argc && strcmp(argv[i], "--blocksize") == 0) {
      int size;
      if (sscanf(argv[i + 1], "%d", &size) == 1) {
        block_size = size;
      } else {
        error("invalid size");
      }
      i++;
    }
    else if (strcmp(argv[i], "-c") == 0
             || strcmp(argv[i], "--stdout") == 0
             || strcmp(argv[i], "--to-stdout") == 0) {
      output = "-";
    }
    else if (i + 1 < argc && strcmp(argv[i], "--output") == 0) {
      output = argv[i + 1];
      i++;
    }
    else if (i + 1 < argc && strcmp(argv[i], "--input") == 0) {
      files[nfiles++] = i;
    }
    else if (argv[i][0] == '-'
             && ( argv[i][1] >= '0' && argv[i][1] <= '9' )
             && argv[i][2] == '\0'
             ) {
      int level = argv[i][1] - '0';
      perfs = level >= 2 ? 2 : 1;
      error("invalid option");
    }
    else if (argv[i][0] == '-' && argv[i][1] == '-') {
      error("invalid option");
    }
    else {
      files[nfiles++] = i;
    }
  }

  /* list mode: only read headers */
  if (list) {
    inbufsize = fastlzlibGetHeaderSize();
    output = NULL;
  }

  /* rock'in */
  if (nfiles != 0) {
    FILE *outstream = NULL;
    int closeoutstream = 0;
    Bytef *buf = malloc(inbufsize);
    Bytef *dest = malloc(outbufsize);
    int i;
    zfast_stream stream;
    memset(&stream, 0, sizeof(stream));
    
    if (compress) {
      if (fastlzlibCompressInit2(&stream, perfs, block_size) != Z_OK) {
        flzerror(&stream, "unable to initialize the compressor");
      }
    } else {
      if (fastlzlibDecompressInit2(&stream, block_size) != Z_OK) {
        flzerror(&stream, "unable to initialize the uncompressor");
      }
    }

    if (fastlzlibSetCompressor(&stream, type) != Z_OK) {
      flzerror(&stream, "unable to initialize the specified compressor");
    }

    if (output != NULL) {
      if (strcmp(output, "-") == 0) {
        outstream = stdout;
        closeoutstream = 0;
      } else {
        outstream = fopen(output, "wb");
        if (outstream == NULL) {
          syserror("can not open output file");
        }
        closeoutstream = 1;
      }
    }
    
    for(i = 0 ; i < nfiles ; i++, fastlzlibReset(&stream),
          stream.total_in = stream.total_out = 0) {
      FILE *instream;
      int closeinstream;
      const char*const filename = argv[files[i]];
      uLong total_out = 0;
      uLong total_in = 0;
     
      if (strcmp(filename, "-") == 0) {
        instream = stdin;
        closeinstream = 0;
      } else {
        instream = fopen(filename, "rb");
        if (instream == NULL) {
          syserror("can not open input file");
        }
        closeinstream = 1;
      }

      if (instream != NULL) {
        while(!feof(instream)) {
          int n = fread(buf, 1, inbufsize, instream);
          const int is_eof = feof(instream);
          if (n >= 0) {

            /* list mode (scan stream without reading/processing) */
            if (list) {
              /* next block */
              uInt compressed_size;
              uInt uncompressed_size;
              if (n != (int) inbufsize) {
                error("truncated input");
              }
              if (fastlzlibGetStreamInfo(buf, n, &compressed_size,
                                        &uncompressed_size) != Z_OK) {
                error("stream read error");
              }
              fprintf(stdout, "%s block at %u ([%u .. %u[):"
                      "\tcompressed=%u\tuncompressed=%u"
                      "\t[block_size=%u]\n",
                      compressed_size != uncompressed_size 
                      ? "compressed" : "uncompressed",
                      (int) total_in,
                      (int) total_out,
                      (int) ( total_out + uncompressed_size ),
                      (int) compressed_size,
                      (int) uncompressed_size,
                      fastlzlibGetStreamBlockSize(buf, n));

              /* check eof consistency */
              if (compressed_size == 0 && uncompressed_size == 0) {
                int n = fread(buf, 1, 1, instream);
                const int is_eof = feof(instream);
                if (n != 0 || !is_eof) {
                  error("premature EOF before end of stream");
                }
              }
              else if (is_eof) {
                error("premature end of stream");
              }
              
              /* skip compressed data */
              if (fseek(instream, compressed_size, SEEK_CUR) != 0) {
                if (errno == EBADF) {
                  /* fseek() on stdin */
                  int skip, n;
                  for(skip = compressed_size
                        ; skip > 0
                        && ( n = fread(dest, 1, outbufsize, instream) ) > 0
                        ; skip -= n) ;
                  if (skip != 0) {
                    syserror("seek error");
                  }
                } else {
                  syserror("seek error");
                }
              }
              total_in += n + compressed_size;
              total_out += uncompressed_size;
            }

            /* compress/uncompress mode */
            else {
              int success;
              stream.next_in = buf;
              stream.avail_in = n;
              do {
                stream.next_out = dest;
                stream.avail_out = outbufsize;
                if (compress) {
                  success = fastlzlibCompress(&stream,
                                              is_eof ? Z_FINISH
                                              : ( flush
                                                  ? Z_SYNC_FLUSH
                                                  : Z_NO_FLUSH )
                                              );
                } else {
                  success = fastlzlibDecompress(&stream);
                }

                if (success == Z_STREAM_END) {
                  if (stream.avail_in > 0 || !is_eof) {
                    error("premature EOF before end of stream");
                  }
                }
              
                if (outstream != NULL && stream.next_out != dest) {
                  const size_t len = stream.next_out - dest;
                  if (len > 0) {
                    if (fwrite(dest, 1, len, outstream) != len
                        || ( flush && fflush(outstream) != 0 ) ) {
                      syserror("write error");
                    }
                  }
                }
              } while(success == Z_OK);

              /* Z_BUF_ERROR means that we need to feed more */
              if (success == Z_BUF_ERROR) {
                if (is_eof && stream.avail_out != 0) {
                  error("premature end of stream");
                }
              }
              else if (success < 0) {
                flzerror(&stream, "stream error");
              }
             
            }
            
          } else if (n < 0) {
            syserror("read error");
          }
        }
        
        if (closeinstream && instream != NULL) {
          fclose(instream);
        }

      } else {
        syserror("can not open file");
      }
    }

    /* cleanup */
    if (closeoutstream && outstream != NULL) {
      fclose(outstream);
    }
    fastlzlibEnd(&stream);
    free(buf);
    free(dest);
  } else {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  free(files);
  return EXIT_SUCCESS;
}

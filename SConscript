############################## ngmime ##############################

# import all exported variables
Import('*')

# build
e = env.Clone()
e.Remove(CCFLAGS = [ '-ansi' ])
e.BuildSharedLibrary(name='ngfastlz',
                     sources='zlibfast.c',
                     depend='',
                     defines=['FASTLZ_INCLUDE_CONF_H'],
                     description='FastLZ - lightning-fast lossless compression library'
                     )
# tools
if False:
    e.BuildExecutable(name='6pack',
                      sources='6pack.c',
                      depend='ngfastlz',
                      description='File compressor using FastLZ'
                      )
    e.BuildExecutable(name='6unpack',
                      sources='6unpack.c',
                      depend='ngfastlz',
                      description='File decompressor using FastLZ'
                      )

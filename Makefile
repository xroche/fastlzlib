###############################################################################
#
#                        "FastLZ" compression library
#
###############################################################################

SRCS = fastlzlib.c lz4/lz4.c lz4/lz4hc.c fastlz/fastlz.c

TARGET_LIB = libfastlz.so
OBJS = $(SRCS:.c=.o)

CFLAGS = -fPIC -O3 -g -W -Wall -Wextra -Werror -Wno-unused-function -pthread -DZFAST_USE_LZ4 -DZFAST_USE_FASTLZ
LDFLAGS = -shared -rdynamic

RM = rm -f

all: fastlzcat

${TARGET_LIB}: $(OBJS)
	$(CC) ${LDFLAGS} -Wl,-soname=libfastlz.so -o $@ $^

fastlzcat: ${TARGET_LIB} fastlzcat.o
	$(CC) -o $@ $^ -L. -lfastlz

.PHONY: clean
clean:
	-${RM} $(OBJS) *.o *.obj *.so* *.dll *.exe *.pdb *.exp *.lib fastlzcat

tar:
	rm -f fastlzlib.tgz
	tar cvfz fastlzlib.tgz fastlzlib.txt fastlzlib.c fastlzlib.h fastlzlib-zlib.h fastlzcat.c Makefile LICENSE

# to be started in a visual studio command prompt
visualcpp:
	cl.exe -nologo -c -MD -O2 -W3 \
		-D_WINDOWS -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
		-D_CRT_SECURE_NO_WARNINGS \
		-DFASTLZ_DLL -DZFAST_USE_LZ4  -DZFAST_USE_FASTLZ \
		$(SRCS)
	link.exe -nologo -dll \
		-out:fastlz.dll \
		-implib:fastlz.lib \
		-DEBUG -PDB:fastlz.pdb \
		fastlzlib.obj fastlz.obj lz4.obj lz4hc.obj
	mt.exe -nologo -manifest fastlz.dll.manifest \
		"-outputresource:fastlz.dll;2"
	cl.exe -nologo -MD -O2 -W3 \
		-D_WINDOWS -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
		-D_CRT_SECURE_NO_WARNINGS \
		-Fefastlzcat.exe \
		fastlz.lib \
		fastlzcat.c -link
	mt.exe -nologo -manifest fastlzcat.exe.manifest \
		"-outputresource:fastlzcat.exe;1"

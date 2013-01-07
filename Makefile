###############################################################################
#
#                        "FastLZ" compression library
#
###############################################################################

CFILES = fastlzlib.c fastlz/fastlz.c
CFILES_LZ4 = fastlzlib.c lz4/lz4.c lz4/lz4hc.c

all:
	make gcc

clean:
	rm -f *.o *.obj *.so* *.dll *.exe *.pdb *.exp *.lib fastlzcat

tar:
	rm -f fastlzlib.tgz
	tar cvfz fastlzlib.tgz fastlzlib.txt fastlzlib.c fastlzlib.h fastlzlib-zlib.h fastlzcat.c Makefile LICENSE

gcc:
	gcc -c -fPIC -O3 -g \
		-W -Wall -Wextra -Werror \
		-D_REENTRANT \
		$(CFILES)
	gcc -shared -fPIC -O3 -Wl,-O1 -Wl,--no-undefined \
		-rdynamic -shared -Wl,-soname=libfastlz.so \
		fastlzlib.o fastlz.o -o libfastlz.so

	gcc -c -fPIC -O3 -g \
		-W -Wall -Wextra -Werror \
		-D_REENTRANT \
		fastlzcat.c -o fastlzcat.o
	gcc -fPIC -O3 -Wl,-O1 \
		-lfastlz -L. \
		fastlzcat.o -o fastlzcat

# lz4 flavor
	gcc -c -fPIC -O3 -g \
		-W -Wall -Wextra -Werror -Wno-unused-function \
		-D_REENTRANT -DZFAST_USE_LZ4 \
		$(CFILES_LZ4)
	gcc -shared -fPIC -O3 -Wl,-O1 -Wl,--no-undefined \
		-rdynamic -shared -Wl,-soname=liblz4.so \
		fastlzlib.o lz4.o lz4hc.o -o liblz4.so

	gcc -fPIC -O3 -Wl,-O1 \
		-llz4 -L. \
		fastlzcat.o -o lz4cat

# to be started in a visual studio command prompt
visualcpp:
	cl.exe -nologo -c -MD -O2 -W3 \
		-D_WINDOWS -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
		-D_CRT_SECURE_NO_WARNINGS \
		-DFASTLZ_DLL \
		-Fofastlz.obj \
		$(CFILES)
	link.exe -nologo -dll \
		-out:fastlz.dll \
		-implib:fastlz.lib \
		-DEBUG -PDB:fastlz.pdb \
		fastlz.obj
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

# lz4 flavor
	cl.exe -nologo -c -MD -O2 -W3 \
		-D_WINDOWS -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
		-D_CRT_SECURE_NO_WARNINGS \
		-DFASTLZ_DLL \
		-DZFAST_USE_LZ4 \
		-Folz4.obj \
		$(CFILES_LZ4)
	link.exe -nologo -dll \
		-out:lz4.dll \
		-implib:lz4.lib \
		-DEBUG -PDB:lz4.pdb \
		lz4.obj
	mt.exe -nologo -manifest lz4.dll.manifest \
		"-outputresource:lz4.dll;2"
	cl.exe -nologo -MD -O2 -W3 \
		-D_WINDOWS -D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
		-D_CRT_SECURE_NO_WARNINGS \
		-Felz4cat.exe \
		lz4.lib \
		fastlzcat.c -link
	mt.exe -nologo -manifest lz4cat.exe.manifest \
		"-outputresource:lz4cat.exe;1"

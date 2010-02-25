###############################################################################
#
#                        "FastLZ" compression library
#
###############################################################################

CFILES = fastlzlib.c

all:
	make gcc

clean:
	rm -f *.o *.so* *.dll

gcc:
	gcc -c -fPIC -O3 -g \
		-W -Wall -Wextra -Werror \
		-D_REENTRANT \
		fastlzlib.c -o fastlz.o
	gcc -shared -fPIC -O3 -Wl,-O1 -Wl,--no-undefined \
		-rdynamic -shared -Wl,-soname=libfastlz.so \
		fastlz.o -o libfastlz.so

	gcc -c -fPIC -O3 -g \
		-W -Wall -Wextra -Werror \
		-D_REENTRANT \
		fastlzcat.c -o fastlzcat.o
	gcc -fPIC -O3 -Wl,-O1 \
		-lfastlz -L. \
		fastlzcat.o -o fastlzcat


visualcpp:
	cl.exe -nologo -MD -LD -O2 -W3 -Zp4 \
		-D_WIN32_WINNT=0x0400 -DWINVER=0x0400 \
		-Fefastlz.dll \
		-D_WINDOWS \
		$(CFILES) -link

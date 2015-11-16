CC=gcc
osx:
	$(CC) -Wall -o pm -Ofast -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -msse2 -msse3 -framework OpenCL run.c
	$(CC) -Wall -o viewer -Ofast -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio `pkg-config --cflags gtk+-3.0` viewer.c `pkg-config --libs gtk+-3.0`
linux:
	$(CC) -Wall -o pm -O3 -lcfitsio -std=c99 -lOpenCL run.c
	$(CC) -Wall -o viewer -O3 -lcfitsio -std=c99 `pkg-config --cflags gtk+-3.0` viewer.c `pkg-config --libs gtk+-3.0`
clean:
	rm -f ./pm

CC=gcc
osx:
	$(CC) -Wall -o pm -Ofast -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -msse2 -msse3 -framework OpenCL run.c
	$(CC) -Wall -o viewer -Ofast -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -msse2 -msse3 `pkg-config --cflags gtk+-3.0` viewer.c `pkg-config --libs gtk+-3.0`
linux:
	$(CC) -Wall -o pm -O3 -std=c99 -lOpenCL run.c -lcfitsio -msse2 -msse3 -lm
	$(CC) -Wall -o viewer -O3 -std=c99 -msse2 -msse3 `pkg-config --cflags gtk+-3.0` viewer.c `pkg-config --libs gtk+-3.0` -lcfitsio -lm
clean:
	rm -f ./pm

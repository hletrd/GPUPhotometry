CC=gcc
osx:
	$(CC) -Wall -o pm -Ofast -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -msse2 -msse3 -framework OpenCL run.c
linux:
	$(CC) -Wall -o pm -O3 -lcfitsio -lOpenCL run.c
clean:
	rm -f ./pm

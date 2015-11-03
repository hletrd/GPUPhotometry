CC=gcc
all:
	$(CC) -Wall -o pm -O3 -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -framework OpenCL run.c
clean:
	rm -f ./pm

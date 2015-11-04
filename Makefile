CC=gcc
osx:
	$(CC) -Wall -o pm -O3 -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -framework OpenCL run.c
linux:
	$(CC) -Wall -o pm -O3 -lcfitsio -lOpenCL run.c
clean:
	rm -f ./pm

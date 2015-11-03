CC=gcc
all:
	$(CC) -Wall -o pm -O3 -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -framework OpenCL run.c
	$(CC) -Wall `pkg-config --cflags --libs gtk+-3.0` -o viewer -O3 -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio viewer.c
clean:
	rm -f ./pm

CC=gcc
all:
	$(CC) -Wall `pkg-config --cflags --libs gtk+-3.0` -o pm -O2 -L/usr/local/Cellar/cfitsio/3.370/lib -lcfitsio -framework OpenCL run.c
clean:
	rm -f ./pm

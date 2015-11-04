# GPUPhotometry
 * needs CFITSIO, OpenCL to be built.
 * Performs bias, dark, flat calibration automatically
  * Uses OpenCL to achieve high performance.

## How to configure
 * 2nd to 5th line of config.txt is count of each frames.
  * 1st: preferred OpenCL device (CPU / GPU)
  * 2nd: count of bias frames
  * 3rd: count of dark frames
  * 4th: count of flat frames
  * 5th: count of light frames
 * from 6th line, type in filenames in order.

## How to install dependencies
* OS X
```
brew install cfitsio
```
* Linux
 * Download latest CFITSIO from the [link](ftp://heasarc.gsfc.nasa.gov/software/fitsio/c/cfitsio_latest.tar.gz)
 * Run followings in the directory with downloaded file
```
tar -xvzf cfitsio*.tar.gz
cd ./cfitsio*
./configure --prefix=/usr
make
make install
```

## How to compile
* In OS X, type
```
make osx
```
* In Linux, type

## How to run
```
./pm config.txt
```
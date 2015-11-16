# GPUPhotometry & FITS viewer
## GPUPhotometry
 * Needs CFITSIO, OpenCL to be built.
 * Performs bias, dark, flat calibration.
  * Uses OpenCL to achieve high performance.

### Installing dependencies
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

### How to write config file
 * 2nd to 5th line of config.txt is count of each frames.
  * 1st: preferred OpenCL device (CPU / GPU)
  * 2nd: count of bias frames
  * 3rd: count of dark frames
  * 4th: count of flat frames
  * 5th: count of light frames
 * from 6th line, type in filenames in order.

### How to build
* OS X
```
make osx
```
* Linux
```
make linux
```

### How to run
```
./pm config.txt
# You can give the name of your config file.
```

## FITS viewer
 * Needs CFITSIO, GTK+ 3.0 to be built.
 * Reads fits file, and can perform differential photometry.

### Installing dependencies
* OS X
```
brew install cfitsio
brew install gtk+3
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
 * GTK+3 is installed by default on ubuntu
 * It is not recommended to install GTK+3 on CentOS 6. Please consider using CentOS 7, on which GTK+3 is installed by default.

### Running
* ./viewer (filename of FITS file) (magnification)
```
./viewer myfitsfile.fit 0.25
```

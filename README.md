# Photometry
 * needs cfitsio to be built.
 * Performs bias, dark, flat calibration automatically

## How to configure
 * 1st to 4th line of config.txt is number of each frames.
  * 1st: number of bias frames
  * 2nd: number of dark frames
  * 3rd: number of flat frames
  * 4th: number of light frames
 * from 5th line, type in filenames in order.

## How to compile
```
make
```

## How to run
```
./pm config.txt
```
# findfire
Analysis of GOES-R/S NetCDF4 Fire Detection Characteristics files.

(Goal - we're not there yet)

Given a directory containing *-FDCC-*, *-FDCF-*, or *-FDCM-* files (Fire Detection Characteristics)
from GOES-R (GOES-16) and GOES-S (GOES-17) satellites, this program will analyze all of them in 
chronological order. The analysis finds clusters of pixels that are connected and analyzes their 
mean latitude, mean longitude, and total fire power in megawatts. Then the points in the time series
are connected to track individual fires.

This initial version will treat fires from each satellite (GOES-16 and GOES-17) independently as
well as fire from each scanning sector (CONUS [FDCC], Full Disk [FDCF], and Mesosector [FDCM])
independenly. Later versions may try to combine the time series from different satellites and
sectors together.

This initial version will also rely on the file naming convention used by the NOAA Big Data
initiative to detect satellite, sector, scan start, and scan end times. Later versions may use
attributes in the NetCDF4 to detect these properties internally.


## Dependencies

### C Libraries

#### GDAL (3.2.2 or later used in development)
 This is critical for accessing and geo-referencing the data. Whatever version of GDAL you're using,
 it must have support for NetCDF4 installed as well. This shouldn't be a problem since that is the
 default anyway.


#### SQLITE3
 sqlite3 is used to keep track of the detected fires so they can be connected and tracked throughout
 time. (Planned).

### Rust crates available on crates.io
chrono = "^0.4.19"

### gdal and gdal-sys
 The Rust interface to GDAL.

### rusqlite
 The rust interface to SQLITE3

### string-error
 To simplifiy some error handling. I'll likely develop my own error type and remove this eventually.

### walkdir
 For listing all the files in the data directory. Currently this may not be necessary, but in the 
 future I may go to a more structured directory tree for organizing the data instead of putting all
 the files in the same directory. Then this will be useful for walking the directory tree.

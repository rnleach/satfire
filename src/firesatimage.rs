use crate::{error::FindFireError, firepoint::FirePoint};

use std::{error::Error, path::Path};

use chrono::naive::NaiveDateTime;
use gdal::{raster::Buffer, Dataset};

pub struct FireSatImage {
    dataset: Dataset,
    satellite: &'static str,
    sector: &'static str,
    start: NaiveDateTime,
    end: NaiveDateTime,
}

impl FireSatImage {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self, Box<dyn Error>> {
        let pth: &Path = path.as_ref();
        let fname = if pth.exists() && pth.is_file() {
            Ok(pth
                .file_name()
                .ok_or(FindFireError {
                    msg: "Path was not a file",
                })?
                .to_string_lossy())
        } else {
            Err(FindFireError {
                msg: "Path isn't a file or doesn't exist",
            })
        }?;

        let open_path = format!("NETCDF:\"{}\":Power", pth.to_string_lossy());
        let open_path = std::path::PathBuf::from(&open_path);
        let dataset = Dataset::open(&open_path)?;

        let satellite = Self::find_satellite_name(&fname)?;
        let sector = Self::find_sector_name(&fname)?;

        let start = FireSatImage::find_start_time(&fname)?;
        let end = FireSatImage::find_end_time(&fname)?;

        Ok(FireSatImage {
            dataset,
            satellite,
            sector,
            start,
            end,
        })
    }

    pub fn extract_fire_points(&self) -> Result<Vec<FirePoint>, Box<dyn Error>> {
        let mut points = vec![];

        let src_srs = self.dataset.spatial_ref()?;
        let dst_srs = gdal::spatial_ref::SpatialRef::from_epsg(4326)?;
        let trans = gdal::spatial_ref::CoordTransform::new(&src_srs, &dst_srs)?;
        let gtrans = self.dataset.geo_transform()?;

        let rasterband = self.dataset.rasterband(1)?;
        let Buffer {
            data,
            size: (x_size, y_size),
        } = rasterband.read_band_as::<f64>()?;

        assert_eq!(x_size, rasterband.x_size());
        assert_eq!(y_size, rasterband.y_size());

        for j in 0..y_size {
            for i in 0..x_size {
                let power = data[j * x_size + i];
                if power > 0.0 {
                    let mut xp: [f64; 1] =
                        [gtrans[0] + gtrans[1] * i as f64 + j as f64 * gtrans[2]];
                    let mut yp: [f64; 1] =
                        [gtrans[3] + gtrans[4] * i as f64 + j as f64 * gtrans[5]];
                    let mut zp: [f64; 1] = [0.0];

                    let _ = trans.transform_coords(&mut xp, &mut yp, &mut zp);

                    points.push(FirePoint {
                        lat: xp[0],
                        lon: yp[0],
                        power,
                        x: i as isize,
                        y: j as isize,
                    });
                }
            }
        }

        Ok(points)
    }

    pub fn start(&self) -> NaiveDateTime {
        self.start
    }
    pub fn end(&self) -> NaiveDateTime {
        self.end
    }
    pub fn satellite(&self) -> &'static str {
        self.satellite
    }
    pub fn sector(&self) -> &'static str {
        self.sector
    }

    /**
     * Parse the file name and find the scan start time.
     */
    pub fn find_start_time(fname: &str) -> Result<NaiveDateTime, FindFireError> {
        if let Some(i) = fname.find("_s") {
            let start = i + 2;
            let end = start + 13;
            let date_str = &fname[start..end];

            match NaiveDateTime::parse_from_str(date_str, "%Y%j%H%M%S") {
                Ok(st) => Ok(st),
                Err(_) => Err(FindFireError {
                    msg: "error parsing start time from file",
                }),
            }
        } else {
            Err(FindFireError {
                msg: "invalid filename format",
            })
        }
    }

    /**
     * Parse the file name and find the scan end time.
     */
    fn find_end_time(fname: &str) -> Result<NaiveDateTime, FindFireError> {
        if let Some(i) = fname.find("_e") {
            let start = i + 2;
            let end = start + 13;
            let date_str = &fname[start..end];

            match NaiveDateTime::parse_from_str(date_str, "%Y%j%H%M%S") {
                Ok(st) => Ok(st),
                Err(_) => Err(FindFireError {
                    msg: "error parsing start time from file",
                }),
            }
        } else {
            Err(FindFireError {
                msg: "invalid filename format",
            })
        }
    }

    fn find_satellite_name(fname: &str) -> Result<&'static str, Box<dyn Error>> {
        // Satellites
        const G16: &str = "G16";
        const G17: &str = "G17";

        if fname.contains(G16) {
            Ok(G16)
        } else if fname.contains(G17) {
            Ok(G17)
        } else {
            Err(Box::new(FindFireError {
                msg: "Invalid file name, no satellite description.",
            }))
        }
    }

    fn find_sector_name(fname: &str) -> Result<&'static str, Box<dyn Error>> {
        // Sectors
        const CONUS: &str = "FDCC";
        const FULL_DISK: &str = "FDCF";
        const MESO: &str = "FDCM";

        if fname.contains(CONUS) {
            Ok(CONUS)
        } else if fname.contains(FULL_DISK) {
            Ok(FULL_DISK)
        } else if fname.contains(MESO) {
            Ok(MESO)
        } else {
            Err(Box::new(FindFireError {
                msg: "Invalid file name, no satellite sector description.",
            }))
        }
    }
}

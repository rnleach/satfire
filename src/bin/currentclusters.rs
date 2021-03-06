use clap::Parser;
use log::info;
use satfire::{
    BoundingBox, ClusterDatabase, Coord, Geo, KmlWriter, KmzFile, SatFireResult, Satellite, Sector,
};
use simple_logger::SimpleLogger;
use std::{
    fmt::{self, Display, Write},
    path::PathBuf,
};

/*-------------------------------------------------------------------------------------------------
 *                                     Command Line Options
 *-----------------------------------------------------------------------------------------------*/

///
/// Export clusters from most recent image into a KMZ file.
///
/// This program will export all the clusters from the latest image in the database for a given
/// satellite and sector as KMZ.
///
#[derive(Debug, Parser)]
#[clap(bin_name = "currentclusters")]
#[clap(author, version, about)]
struct CurrentClustersOptionsInit {
    /// The path to the cluster database file.
    ///
    /// If this is not specified, then the program will check for it in the "CLUSTER_DB"
    /// environment variable.
    #[clap(short, long)]
    #[clap(env = "CLUSTER_DB")]
    cluster_store_file: PathBuf,

    /// The path to a KMZ file to produce from this run.
    ///
    /// If this is not specified, then the program will create one automatically by replacing the
    /// file extension on the store_file with "*.kmz".
    #[clap(short, long)]
    kmz_file: Option<PathBuf>,

    /// The satellite to export the data for.
    ///
    /// If this is not specified, then it will default to GOES-17. Allowed values are G16 and G17.
    #[clap(parse(try_from_str=parse_satellite))]
    #[clap(default_value_t=Satellite::G17)]
    sat: Satellite,

    /// The satellite sector to export the data for.
    ///
    /// If this is not specified, then it will default to full disk. Allowed values are FDCF (for
    /// full disk), FDCC (for CONUS), FDCM1 (for meso-sector 1), and FDCM2 (for meso-sector 2).
    #[clap(parse(try_from_str=parse_sector))]
    #[clap(default_value = "FDCF")]
    sector: Sector,

    /// Verbose output
    #[clap(short, long)]
    verbose: bool,
}

fn parse_satellite(sat: &str) -> SatFireResult<Satellite> {
    let sat = Satellite::string_contains_satellite(sat)
        .ok_or_else(|| format!("Argument is not a valid satellite name: {}", sat))?;
    Ok(sat)
}

fn parse_sector(sector: &str) -> SatFireResult<Sector> {
    let sector = Sector::string_contains_sector(sector)
        .ok_or_else(|| format!("Argument is not a valid sector name: {}", sector))?;
    Ok(sector)
}

#[derive(Debug)]
struct CurrentClustersOptionsChecked {
    /// The path to the database file.
    cluster_store_file: PathBuf,

    /// The path to a KMZ file to produce from this run.
    kmz_file: PathBuf,

    /// The satellite.
    sat: Satellite,

    /// The Sector.
    sector: Sector,

    /// Verbose output
    verbose: bool,
}

impl Display for CurrentClustersOptionsChecked {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        writeln!(f, "\n")?; // yes, two blank lines.
        writeln!(f, "    Database: {}", self.cluster_store_file.display())?;
        writeln!(f, "  Output KMZ: {}", self.kmz_file.display())?;
        writeln!(f, "   Satellite: {}", self.sat.name())?;
        writeln!(f, "      Sector: {}", self.sector.name())?;
        writeln!(f, "\n")?; // yes, two blank lines.

        Ok(())
    }
}

/// Get the command line arguments and check them.
///
/// If there is missing data, try to fill it in with environment variables.
fn parse_args() -> SatFireResult<CurrentClustersOptionsChecked> {
    let CurrentClustersOptionsInit {
        cluster_store_file,
        kmz_file,
        sat,
        sector,
        verbose,
    } = CurrentClustersOptionsInit::parse();

    let kmz_file = match kmz_file {
        Some(v) => v,
        None => {
            let mut clone = cluster_store_file.clone();
            clone.set_extension("kmz");
            clone
        }
    };

    let checked = CurrentClustersOptionsChecked {
        cluster_store_file,
        kmz_file,
        sat,
        sector,
        verbose,
    };

    if verbose {
        info!("{}", checked);
    }

    Ok(checked)
}

/*-------------------------------------------------------------------------------------------------
 *                                             MAIN
 *-----------------------------------------------------------------------------------------------*/
fn main() -> SatFireResult<()> {
    SimpleLogger::new().init()?;

    let opts = parse_args()?;

    //
    // Load the data, the most recent clusters.
    //
    let db = ClusterDatabase::connect(&opts.cluster_store_file)?;
    let latest = db.newest_scan_start(opts.sat, opts.sector)?;
    let latest_start = latest - chrono::Duration::seconds(1);
    let latest_end = latest + chrono::Duration::hours(1);

    // Default to cover the whole globe
    let region = BoundingBox {
        ll: Coord {
            lat: -90.0,
            lon: -180.0,
        },
        ur: Coord {
            lat: 90.0,
            lon: 180.0,
        },
    };

    let mut clusters: Vec<_> = db
        .query_clusters(
            Some(opts.sat),
            Some(opts.sector),
            latest_start,
            latest_end,
            region,
        )?
        .rows()?
        .filter_map(|res| res.ok())
        .collect();

    drop(db);

    clusters.sort_unstable_by(|a, b| a.power.partial_cmp(&b.power).unwrap());

    if opts.verbose {
        info!("Retrieved {} clusters.", clusters.len());
    }

    //
    // Output the KMZ
    //
    let mut kfile = KmzFile::new(&opts.kmz_file)?;

    kfile.start_style(Some("fire"))?;
    kfile.create_icon_style(
        Some("http://maps.google.com/mapfiles/kml/shapes/firedept.png"),
        1.3,
    )?;
    kfile.finish_style()?;

    kfile.start_folder(Some(opts.sat.name()), None, false)?;

    let mut name = String::new();
    let mut description = String::new();
    for cluster in clusters {
        name.clear();
        let _ = write!(&mut name, "{:.0}MW", cluster.power);

        description.clear();
        let _ = write!(
            &mut description,
            concat!(
                "<h3>Cluster Power: {:.0}MW</h3>",
                "<h3>Max Scan Angle: {:.2}&deg;</h3>",
                "<h3>Max Temperature: {:.2}&deg;K</h3>",
            ),
            cluster.power, cluster.scan_angle, cluster.max_temperature,
        );

        kfile.start_folder(Some(&name), None, false)?;

        let centroid = cluster.pixels.centroid();
        kfile.start_placemark(None, Some(&description), Some("#fire"))?;
        kfile.create_point(centroid.lat, centroid.lon, 0.0)?;
        kfile.finish_placemark()?;
        cluster.pixels.kml_write(&mut kfile);
        kfile.finish_folder()?;
    }

    kfile.finish_folder()?;

    Ok(())
}

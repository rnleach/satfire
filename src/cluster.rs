/*!
 * Types and functions for working with clusters.
 *
 * A cluster describes the aggregate properties of a connected group (or cluster) of FirePoint
 * objects.
 */
use crate::{
    firepoint::FirePoint,
    satellite::{Satellite, Sector},
    FireSatImage,
};
use chrono::NaiveDateTime;
use geo::{
    algorithm::centroid::Centroid, point, Coordinate, LineString, MultiPolygon, Point, Polygon,
};
use kd_tree::KdPoint;
use std::{error::Error, iter::FromIterator};

/**
 * The aggregate properties of a connected group of FirePoint objects.
 */
#[derive(Clone, Debug)]
pub struct Cluster {
    /// The satellite that this cluster was analyzed from.
    pub satellite: Satellite,
    /// The scan sector this cluster was analyzed from.
    pub sector: Sector,
    /// The start time of the scan this cluster was detected on.
    pub scan_start_time: NaiveDateTime,
    /// Perimeter
    pub perimeter: MultiPolygon<f64>,
    /// Centroid
    pub centroid: Point<f64>,
    /// Total (sum) of the fire power of the points in the cluster in megawatts.
    pub power: f64,
    /// The number of points that are in this cluster.
    pub count: i32,
}

impl KdPoint for Cluster {
    type Scalar = f64;
    type Dim = typenum::U2;

    fn at(&self, k: usize) -> Self::Scalar {
        match k {
            0 => self.centroid.x(),
            1 => self.centroid.y(),
            _ => unreachable!(),
        }
    }
}

impl Cluster {
    /**
     * Analyze a FireSatImage and return a list of clusters.
     *
     * #Arguments
     * fsat - the already loaded image data.
     */
    pub fn from_fire_sat_image(fsat: &FireSatImage) -> Result<Vec<Self>, Box<dyn Error>> {
        let satellite = fsat.satellite();
        let sector = fsat.sector();
        let scan_start = fsat.start();

        let points = fsat.extract_fire_points()?;
        let clusters = Cluster::from_fire_points(points, scan_start, satellite, sector);

        Ok(clusters)
    }

    fn from_fire_points(
        mut points: Vec<FirePoint>,
        scan_start_time: NaiveDateTime,
        satellite: Satellite,
        sector: Sector,
    ) -> Vec<Self> {
        let mut clusters: Vec<Self> = vec![];
        let mut cluster_index_coords: Vec<(isize, isize)> = vec![];
        let mut cluster_polys: Vec<Polygon<f64>> = vec![];

        const NULL_PT: FirePoint = FirePoint {
            x: 0,
            y: 0,
            power: f64::NAN,
            lats: [f64::NAN; 4],
            lons: [f64::NAN; 4],
        };

        for i in 0..points.len() {
            if points[i].x == 0 && points[i].y == 0 {
                continue;
            }

            let curr_pt = std::mem::replace(&mut points[i], NULL_PT);

            let mut count = 1;
            let mut power = curr_pt.power;

            let poly: LineString<_> = curr_pt
                .lats
                .iter()
                .cloned()
                .zip(curr_pt.lons.iter().cloned())
                .map(|(lat, lon)| Coordinate { x: lon, y: lat })
                .collect();

            cluster_polys.push(Polygon::new(poly, vec![]));

            cluster_index_coords.push((curr_pt.x, curr_pt.y));

            loop {
                let mut some_found = false;
                for j in (i + 1)..points.len() {
                    // Skip NULL_PT values
                    if points[j].x == 0 && points[j].y == 0 {
                        continue;
                    }

                    let mut in_cluster = false;
                    for (x, y) in &cluster_index_coords {
                        let dx = (x - points[j].x).abs();
                        let dy = (y - points[j].y).abs();

                        if dx <= 1 && dy <= 1 {
                            in_cluster = true;
                            break;
                        }
                    }

                    if in_cluster {
                        let candidate = std::mem::replace(&mut points[j], NULL_PT);
                        count += 1;
                        power += candidate.power;

                        let poly: LineString<_> = candidate
                            .lats
                            .iter()
                            .cloned()
                            .zip(candidate.lons.iter().cloned())
                            .map(|(lat, lon)| Coordinate { x: lon, y: lat })
                            .collect();

                        cluster_polys.push(Polygon::new(poly, vec![]));

                        cluster_index_coords.push((candidate.x, candidate.y));
                        some_found = true;
                    }
                }

                if !some_found {
                    break;
                }
            }

            let perimeter = MultiPolygon::from_iter(cluster_polys.drain(..));
            let centroid = perimeter.centroid().unwrap_or(point!(x: 0.0, y: 0.0));

            let curr_clust = Cluster {
                satellite,
                sector,
                scan_start_time,
                count,
                power,
                perimeter,
                centroid,
            };

            clusters.push(curr_clust);
            cluster_index_coords.truncate(0);
        }

        clusters
    }
}

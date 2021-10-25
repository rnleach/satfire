#include "geo.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <tgmath.h>

/*-------------------------------------------------------------------------------------------------
 *                                    Helper types and functions
 *-----------------------------------------------------------------------------------------------*/
struct Line {
    struct Coord start;
    struct Coord end;
};

struct IntersectResult {
    struct Coord intersection;
    char const *msg;
    bool does_intersect;
    bool intersect_is_endpoints;
};

static struct IntersectResult
lines_intersection(struct Line l1, struct Line l2)
{
    struct IntersectResult result = {.intersection = (struct Coord){.lat = NAN, .lon = NAN},
                                     .does_intersect = false,
                                     .intersect_is_endpoints = false,
                                     .msg = "nothing to report"};

    double m1 = (l1.end.lat - l1.start.lat) / (l1.end.lon - l1.start.lon);
    double m2 = (l2.end.lat - l2.start.lat) / (l2.end.lon - l2.start.lon);

    double x1 = l1.start.lon;
    double y1 = l1.start.lat;
    double x2 = l2.start.lon;
    double y2 = l2.start.lat;

    if (m1 == m2 || (isinf(m1) && isinf(m2))) {
        // NOTE: This also captures colinear cases.
        result.does_intersect = false;
        result.msg = "parallel lines";
        return result;
    }

    double x0 = NAN;
    double y0 = NAN;
    if (isinf(m1)) {
        // l1 is vertical
        x0 = l1.start.lon;
        y0 = m2 * (x0 - x2) + y2;
    } else if (isinf(m2)) {
        // l2 is vertical
        x0 = l2.start.lon;
        y0 = m1 * (x0 - x1) + y1;
    } else {
        x0 = (y2 - y1 + m1 * x1 - m2 * x2) / (m1 - m2);
        y0 = m1 * (x0 - x1) + y1;
    }

    result.intersection = (struct Coord){.lat = y0, .lon = x0};

    if (y0 > fmax(l1.start.lat, l1.end.lat) || y0 < fmin(l1.start.lat, l1.end.lat) ||
        x0 > fmax(l1.start.lon, l1.end.lon) || x0 < fmin(l1.start.lon, l1.end.lon)) {

        // Test to make sure we are within the limits of l1

        result.does_intersect = false;
        result.msg = "intersection point outside line segment";
    } else if (y0 > fmax(l2.start.lat, l2.end.lat)
               // Test to make sure we are within the limits of l2
               || y0 < fmin(l2.start.lat, l2.end.lat) || x0 > fmax(l2.start.lon, l2.end.lon) ||
               x0 < fmin(l2.start.lon, l2.end.lon)) {

        result.does_intersect = false;
        result.msg = "intersection point outside line segment";
    } else {
        result.does_intersect = true;

        bool is_l1_endpoint =
            ((result.intersection.lat == l1.start.lat && result.intersection.lon == l1.start.lon) ||
             (result.intersection.lat == l1.end.lat && result.intersection.lon == l1.end.lon));

        bool is_l2_endpoint =
            ((result.intersection.lat == l2.start.lat && result.intersection.lon == l2.start.lon) ||
             (result.intersection.lat == l2.end.lat && result.intersection.lon == l2.end.lon));

        if (is_l1_endpoint && is_l2_endpoint) {
            result.intersect_is_endpoints = true;
        }
    }

    return result;
}

static struct Coord
triangle_centroid(struct Coord v1, struct Coord v2, struct Coord v3)
{
    double avg_lat = (v1.lat + v2.lat + v3.lat) / 3.0;
    double avg_lon = (v1.lon + v2.lon + v3.lon) / 3.0;

    return (struct Coord){.lat = avg_lat, .lon = avg_lon};
}

struct BoundingBox {
    struct Coord ll;
    struct Coord ur;
};

static struct BoundingBox
sat_pixel_bounding_box(struct SatPixel const pxl[static 1])
{
    double xmax = fmax(pxl->ur.lon, pxl->lr.lon);
    double xmin = fmin(pxl->ul.lon, pxl->ll.lon);
    double ymax = fmax(pxl->ur.lat, pxl->ul.lat);
    double ymin = fmin(pxl->lr.lat, pxl->ll.lat);

    struct Coord ll = {.lat = ymin, .lon = xmin};
    struct Coord ur = {.lat = ymax, .lon = xmax};

    return (struct BoundingBox){.ll = ll, .ur = ur};
}

static bool
bounding_box_contains_coord(struct BoundingBox const box, struct Coord const coord)
{
    bool lon_in_range = coord.lon < box.ur.lon && coord.lon > box.ll.lon;
    bool lat_in_range = coord.lat < box.ur.lat && coord.lat > box.ll.lat;

    return lon_in_range && lat_in_range;
}

/*-------------------------------------------------------------------------------------------------
 *                                         Coordinates
 *-----------------------------------------------------------------------------------------------*/
bool
coord_are_close(struct Coord left, struct Coord right, double eps)
{
    double lat_diff = left.lat - right.lat;
    double lon_diff = left.lon - right.lon;
    double distance_squared = lat_diff * lat_diff + lon_diff * lon_diff;

    return distance_squared <= (eps * eps);
}

/*-------------------------------------------------------------------------------------------------
 *                                         SatPixels
 *-----------------------------------------------------------------------------------------------*/
struct Coord
sat_pixel_centroid(struct SatPixel pxl[static 1])
{
    /* Steps to calculatule the centroid of a quadrilateral.
     *
     *  1) Break the quadrilateral into two triangles by creating a diagonal.
     *  2) Calculate the centroid of each triangle by taking the average of it's 3 Coords
     *  3) Create a line connecting the centroids of each triangle.
     *  4) Repeat the process by creating the other diagonal.
     *  5) Find the intersection of the two resulting lines, that is the centroid of the
     *     quadrilateral.
     */

    struct Coord t1_c = triangle_centroid(pxl->ul, pxl->ll, pxl->lr);
    struct Coord t2_c = triangle_centroid(pxl->ul, pxl->ur, pxl->lr);
    struct Line diag1_centroids = {.start = t1_c, .end = t2_c};

    struct Coord t3_c = triangle_centroid(pxl->ul, pxl->ll, pxl->ur);
    struct Coord t4_c = triangle_centroid(pxl->lr, pxl->ur, pxl->ll);
    struct Line diag2_centroids = {.start = t3_c, .end = t4_c};

    struct IntersectResult res = lines_intersection(diag1_centroids, diag2_centroids);

    assert(res.does_intersect);

    return res.intersection;
}

bool
sat_pixels_approx_equal(struct SatPixel left[static 1], struct SatPixel right[static 1], double eps)
{
    return coord_are_close(left->ul, right->ul, eps) && coord_are_close(left->ur, right->ur, eps) &&
           coord_are_close(left->lr, right->lr, eps) && coord_are_close(left->ll, right->ll, eps);
}

bool
sat_pixel_contains_coord(struct SatPixel const pxl[static 1], struct Coord coord)
{
    // Check if it's outside the bounding box first. This is easy, and if it is,
    // then we already know the answer.
    struct BoundingBox const box = sat_pixel_bounding_box(pxl);

    if (!bounding_box_contains_coord(box, coord)) {
        return false;
    }

    struct Line pxl_lines[4] = {
        (struct Line){.start = pxl->ul, .end = pxl->ur},
        (struct Line){.start = pxl->ur, .end = pxl->lr},
        (struct Line){.start = pxl->lr, .end = pxl->ll},
        (struct Line){.start = pxl->ll, .end = pxl->ul},
    };

    struct Line coord_lines[4] = {
        (struct Line){.start = coord, .end = pxl->ul},
        (struct Line){.start = coord, .end = pxl->ur},
        (struct Line){.start = coord, .end = pxl->ll},
        (struct Line){.start = coord, .end = pxl->lr},
    };

    for (unsigned int i = 0; i < 4; ++i) {
        for (unsigned int j = 0; j < 4; ++j) {
            struct IntersectResult res = lines_intersection(pxl_lines[i], coord_lines[j]);

            if (res.does_intersect && !res.intersect_is_endpoints) {
                return false;
            }
        }
    }

    return true;
}

bool
sat_pixels_overlap(struct SatPixel left[static 1], struct SatPixel right[static 1], double eps)
{
    // Check if they are equal first, then of course they overlap!
    if (sat_pixels_approx_equal(left, right, eps)) {
        return true;
    }

    // If pixels overlap, then at least 1 vertex from one pixel must be inside the boundary of
    // the other pixel or the pixels must have lines that intersect. In the case of one pixel 
    // completely contained inside another (extremely unlikely), there would be no intersections
    // but all the points of one would be contained in another. In any other case, there must be
    // an intersection of lines. 
    //
    // This is all by my own reasoning, not based on any math book or papers on geometry. I'm
    // assuming all pixels are convex quadrilaterals.

    // Check for intersecting lines between the pixels.
    struct Line left_pxl_lines[4] = {
        (struct Line){.start = left->ul, .end = left->ur},
        (struct Line){.start = left->ur, .end = left->lr},
        (struct Line){.start = left->lr, .end = left->ll},
        (struct Line){.start = left->ll, .end = left->ul},
    };

    struct Line right_pxl_lines[4] = {
        (struct Line){.start = right->ul, .end = right->ur},
        (struct Line){.start = right->ur, .end = right->lr},
        (struct Line){.start = right->lr, .end = right->ll},
        (struct Line){.start = right->ll, .end = right->ul},
    };

    for(unsigned i = 0; i < 4; ++i) {
        struct Line left = left_pxl_lines[i];

        for (unsigned j = 0; j < 4; ++j) {
            struct Line right = right_pxl_lines[j];

            struct IntersectResult res = lines_intersection(left, right);

            if (res.does_intersect && !res.intersect_is_endpoints) {
                return true;
            }
        }
    }

    // Checking for intersecting lines didn't find anything. Now try seeing if one pixel is
    // contained in the other pixel.
    struct Coord left_coords[4] = {left->ul, left->ur, left->lr, left->ll};
    for(unsigned i = 0; i < 4; ++i) {
        if (sat_pixel_contains_coord(right, left_coords[i])) {
            return true;
        }
    }

    struct Coord right_coords[4] = {right->ul, right->ur, right->lr, right->ll};
    for(unsigned i = 0; i < 4; ++i) {
        if (sat_pixel_contains_coord(left, right_coords[i])) {
            return true;
        }
    }

    // No intersecting lines and no corners of one pixel contained in the other, so there
    // is no overlap.
    return false;
}

bool
sat_pixels_are_adjacent(struct SatPixel left[static 1], struct SatPixel right[static 1], double eps)
{
    assert(false);
}

/*-------------------------------------------------------------------------------------------------
 *                                         PixelList
 *-----------------------------------------------------------------------------------------------*/
struct PixelList *
pixel_list_new()
{
    assert(false);
}

struct PixelList *
pixel_list_new_with_capacity(size_t capacity)
{
    assert(false);
}

void
pixel_list_destroy(struct PixelList *plist[static 1])
{
    assert(false);
}

struct PixelList *
pixel_list_append(struct PixelList list[static 1], struct SatPixel apix[static 1])
{
    assert(false);
}

void
pixel_list_clear(struct PixelList list[static 1])
{
    assert(false);
}

/*-------------------------------------------------------------------------------------------------
 *                                         Binary Format
 *-----------------------------------------------------------------------------------------------*/
size_t
pixel_list_binary_serialize_buffer_size(struct PixelList plist[static 1])
{
    assert(false);
}

size_t
pixel_list_binary_serialize(struct PixelList plist[static 1], size_t buf_size,
                            unsigned char buffer[buf_size])
{
    assert(false);
}

struct PixelList *
pixel_list_binary_deserialize(size_t buf_size, unsigned char buffer[buf_size])
{
    assert(false);
}

/*-------------------------------------------------------------------------------------------------
 *                                         KML Export
 *-----------------------------------------------------------------------------------------------*/
int
pixel_list_kml_print(FILE *strm, struct PixelList plist[static 1])
{
    assert(false);
}

/*-------------------------------------------------------------------------------------------------
 *                                            Misc
 *-----------------------------------------------------------------------------------------------*/
/** Scaling factor to convert from degrees to radians. */
#define DEG2RAD (2.0L * M_PI / 360.0L)
#define EARTH_RADIUS_KM 6371.0090

double
great_circle_distance(double lat1, double lon1, double lat2, double lon2)
{
    double lat1_r = lat1 * DEG2RAD;
    double lon1_r = lon1 * DEG2RAD;
    double lat2_r = lat2 * DEG2RAD;
    double lon2_r = lon2 * DEG2RAD;

    double dlat2 = (lat2_r - lat1_r) / 2.0;
    double dlon2 = (lon2_r - lon1_r) / 2.0;

    double sin2_dlat = pow(sin(dlat2), 2.0);
    double sin2_dlon = pow(sin(dlon2), 2.0);

    double arc = 2.0 * asin(sqrt(sin2_dlat + sin2_dlon * cos(lat1_r) * cos(lat2_r)));

    return arc * EARTH_RADIUS_KM;
}

#undef EARTH_RADIUS_KM
#undef DEG2RAD

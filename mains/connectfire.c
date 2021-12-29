/** \file connectfire.c
 * \brief Create several time series of fires by temporally connecting clusters (from findfire.c).
 *
 * Connect clusters from the output database of findfire to make time series of fires. Each time
 * series is given an ID and stored in a database with a start date and an end date. In the future
 * other statistics may be added to that database. Another table in the database will record the
 * relationship to clusters by associating a row number from the sqlite database with a fire ID
 * from the database table created by this program.
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "satfire.h"

#include "sf_util.h"

/*-------------------------------------------------------------------------------------------------
 *                          Program Initialization, Finalization, and Options
 *-----------------------------------------------------------------------------------------------*/
static struct ConnectFireOptions {
    char *database_file;
    bool verbose;

} options = {0};

// clang-format off
static GOptionEntry option_entries[] = 
{
    {
        "verbose", 
        'v', 
        G_OPTION_FLAG_NONE, 
        G_OPTION_ARG_NONE, 
        &options.verbose, 
        "Show verbose output.", 
        0
    },

    {NULL}
};
// clang-format on

static void
program_initialization(int argc[static 1], char ***argv)
{
    // Force to use UTC timezone.
    setenv("TZ", "UTC", 1);
    tzset();

    satfire_initialize();

    // Initialize with with environment variables and default values.
    if (getenv("CLUSTER_DB")) {
        asprintf(&options.database_file, "%s", getenv("CLUSTER_DB"));
    }

    // Parse command line options.
    GError *error = 0;
    GOptionContext *context = g_option_context_new("- Temporally connect clusters to form fires.");
    g_option_context_add_main_entries(context, option_entries, 0);
    g_option_context_parse(context, argc, argv, &error);
    Stopif(error, exit(EXIT_FAILURE), "Error parsing options: %s", error->message);
    g_option_context_free(context);

    Stopif(!options.database_file, exit(EXIT_FAILURE), "Invalid, database_file is NULL");

    // Print out options as configured.
    if (options.verbose) {
        fprintf(stdout, "  Database: %s\n", options.database_file);
    }

    satfire_db_initialize(options.database_file);
}

static void
program_finalization()
{
    free(options.database_file);

    satfire_finalize();
}

/*-------------------------------------------------------------------------------------------------
 *                                             Main
 *-----------------------------------------------------------------------------------------------*/
int
main(int argc, char *argv[argc + 1])
{
    program_initialization(&argc, &argv);

    time_t start = 0;
    time_t end = time(0);

    SFDatabaseH db = satfire_db_connect(options.database_file);
    struct SFBoundingBox area = {.ll = (struct SFCoord){.lat = -90.0, .lon = -180.0},
                                 .ur = (struct SFCoord){.lat = 90.0, .lon = 180.0}};

    for (unsigned int sat = 0; sat < SATFIRE_SATELLITE_NUM; sat++) {
        SFClusterDatabaseQueryRowsH rows =
            satfire_cluster_db_query_rows(db, sat, SATFIRE_SECTOR_NONE, start, end, area);
        Stopif(!rows, continue, "Error querying rows for %s, moving on to next satellite.",
               satfire_satellite_name(sat));

        time_t current_time_step = 0;

        struct SFClusterRow *row = 0;
        while ((row = satfire_cluster_db_query_rows_next(rows, row))) {

            time_t start = satfire_cluster_db_satfire_cluster_row_start(row);
            struct SFCoord centroid = satfire_cluster_db_satfire_cluster_row_centroid(row);

            if (start != current_time_step) {
                printf("\n");
                current_time_step = start;
            }

            printf("lat: %10.6lf lon: %11.6lf power: %6.0lf max_temperature: %3.0lf from %s %s %s",
                   centroid.lat, centroid.lon, satfire_cluster_db_satfire_cluster_row_power(row),
                   satfire_cluster_db_satfire_cluster_row_max_temperature(row),
                   satfire_satellite_name(satfire_cluster_db_satfire_cluster_row_satellite(row)),
                   satfire_sector_name(satfire_cluster_db_satfire_cluster_row_sector(row)),
                   ctime(&start));
        }

        int sc = satfire_cluster_db_query_rows_finalize(&rows);
        Stopif(sc, break, "Error finalizing row query, quiting.");
    }

    satfire_db_close(&db);

    program_finalization();

    return EXIT_SUCCESS;
}

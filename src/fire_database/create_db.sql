CREATE TABLE IF NOT EXISTS clusters
(
  rowid          INTEGER PRIMARY KEY AUTOINCREMENT,
  satellite      TEXT    NOT NULL,
  sector         TEXT    NOT NULL,
  mid_point_time INTEGER NOT NULL,
  lat            REAL    NOT NULL,
  lon            REAL    NOT NULL,
  power          REAL    NOT NULL,
  cell_count     INTEGER NOT NULL,
  perimeter      BLOB    NOT NULL
);

CREATE UNIQUE INDEX IF NOT EXISTS no_cluster_dups 
  ON clusters (satellite, sector, mid_point_time, lat, lon);

CREATE TABLE IF NOT EXISTS fires
(
    id            TEXT    PRIMARY KEY,
    satellite,    TEXT    NOT NULL,
    last_observed INTEGER NOT NULL,
    origin_lat    REAL    NOT NULL,
    origin_lon    REAL    NOT NULL,
    perimeter     BLOB    NOT NULL
);

CREATE TABLE IF NOT EXISTS meta
(
    item_name  TEXT PRIMARY KEY,
    item_value NOT NULL
);

CREATE TABLE IF NOT EXISTS associations
(
    cluster_row_id INTEGER,
    fire_id        TEXT,
    FOREIGN KEY (cluster_row_id) REFERENCES clusters (rowid),
    FOREIGN KEY (fire_id) REFERENCES fires (id)
);
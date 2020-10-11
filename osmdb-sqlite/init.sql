/*
 * Copyright (c) 2020 Jeff Boody
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

PRAGMA temp_store_directory = '.';

/*
 * CREATE MAIN TABLES
 */

.print 'CREATE MAIN TABLES'

CREATE TABLE tbl_nodes_coords
(
	nid      INTEGER PRIMARY KEY NOT NULL,
	lat      FLOAT,
	lon      FLOAT
);

CREATE TABLE tbl_nodes_info
(
	nid      INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_nodes_coords,
	class    INTEGER,
	name     TEXT,
	abrev    TEXT,
	ele      INTEGER,
	st       INTEGER,
	min_zoom INTEGER
);

CREATE TABLE tbl_ways
(
	wid      INTEGER PRIMARY KEY NOT NULL,
	class    INTEGER,
	layer    INTEGER,
	name     TEXT,
	abrev    TEXT,
	oneway   INTEGER,
	bridge   INTEGER,
	tunnel   INTEGER,
	cutting  INTEGER,
	selected INTEGER,
	min_zoom INTEGER
);

CREATE TABLE tbl_rels
(
	rid      INTEGER PRIMARY KEY NOT NULL,
	class    INTEGER,
	name     TEXT,
	abrev    TEXT,
	min_zoom INTEGER
);

CREATE TABLE tbl_ways_nds
(
	idx INTEGER,
	wid INTEGER REFERENCES tbl_ways,
	nid INTEGER REFERENCES tbl_nodes_coords
);

CREATE TABLE tbl_nodes_members
(
	rid  INTEGER REFERENCES tbl_rels,
	nid  INTEGER REFERENCES tbl_nodes_coords,
	role INTEGER
);

CREATE TABLE tbl_ways_members
(
	idx  INTEGER,
	rid  INTEGER REFERENCES tbl_rels,
	wid  INTEGER REFERENCES tbl_ways,
	role INTEGER
);

/*
 * CREATE DERIVED TABLES
 */

.print 'CREATE DERIVED TABLES'

CREATE TABLE tbl_ways_range
(
	wid  INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways,
	latT FLOAT,
	lonL FLOAT,
	latB FLOAT,
	lonR FLOAT
);

CREATE TABLE tbl_rels_range
(
	rid  INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_rels,
	latT FLOAT,
	lonL FLOAT,
	latB FLOAT,
	lonR FLOAT
);

/*
 * CREATE WORKING SELECTED TABLES
 * These tables contain references to all nodes/ways which
 * were selected during the initial construction phase and
 * those which were transitively selected by relations or
 * ways.
 */

.print 'CREATE WORKING SELECTED TABLES'

CREATE TABLE tbl_nodes_selected
(
	nid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_nodes_coords
);

CREATE TABLE tbl_ways_selected
(
	wid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways
);

/*
 * WORKING CENTER TABLES
 * These tables contain references to determine which
 * ways can discard their nds and which relations can
 * discard their member ways
 */

.print 'CREATE WORKING CENTER TABLES'

CREATE TABLE tbl_ways_center
(
	wid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways
);

CREATE TABLE tbl_rels_center
(
	rid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_rels
);

/*
 * CREATE WORKING POLYGON TABLES
 * These tables contain references to determine which
 * ways and relations are drawn as polygons.
 */

.print 'CREATE WORKING POLYGON TABLES'

CREATE TABLE tbl_ways_polygon
(
	wid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways
);

CREATE TABLE tbl_rels_polygon
(
	rid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_rels
);

/*
 * IMPORT TABLES
 */

.mode csv
.separator |
.print 'IMPORT tbl_nodes_coords'
.import tbl_nodes_coords.data tbl_nodes_coords
.print 'IMPORT tbl_nodes_info'
.import tbl_nodes_info.data tbl_nodes_info
.print 'IMPORT tbl_ways'
.import tbl_ways.data tbl_ways
.print 'IMPORT tbl_rels'
.import tbl_rels.data tbl_rels
.print 'IMPORT tbl_ways_nds'
.import tbl_ways_nds.data tbl_ways_nds
.print 'IMPORT tbl_nodes_members'
.import tbl_nodes_members.data tbl_nodes_members
.print 'IMPORT tbl_ways_members'
.import tbl_ways_members.data tbl_ways_members
.print 'IMPORT tbl_nodes_selected'
.import tbl_nodes_selected.data tbl_nodes_selected
.print 'IMPORT tbl_ways_selected'
.import tbl_ways_selected.data tbl_ways_selected
.print 'IMPORT tbl_ways_center'
.import tbl_ways_center.data tbl_ways_center
.print 'IMPORT tbl_rels_center'
.import tbl_rels_center.data tbl_rels_center
.print 'IMPORT tbl_ways_polygon'
.import tbl_ways_polygon.data tbl_ways_polygon
.print 'IMPORT tbl_rels_polygon'
.import tbl_rels_polygon.data tbl_rels_polygon

/*
 * CREATE PROCESS INDEXES
 */

.print 'CREATE idx_ways_members'
CREATE INDEX idx_ways_members ON tbl_ways_members (rid);
.print 'CREATE idx_ways_nds'
CREATE INDEX idx_ways_nds ON tbl_ways_nds (wid);

/*
 * PROCESS RANGE
 */

-- compute the range of all ways
.print 'PROCESS tbl_ways_range'
INSERT INTO tbl_ways_range (wid, latT, lonL, latB, lonR)
	SELECT wid, max(lat) AS latT, min(lon) AS lonL, min(lat) AS latB, max(lon) AS lonR
		FROM tbl_ways_nds
		JOIN tbl_nodes_coords USING (nid)
		GROUP BY wid;

-- compute the range of all relations
.print 'PROCESS tbl_rels_range'
INSERT INTO tbl_rels_range (rid, latT, lonL, latB, lonR)
	SELECT rid, max(latT) AS latT, min(lonL) AS lonL, min(latB) AS latB, max(lonR) AS lonR
		FROM tbl_ways_members
		JOIN tbl_ways_range USING (wid)
		GROUP BY rid;

/*
 * PROCESS RELATIONS
 */

-- center large polygon relations
-- large areas are defined to be 50% of the area covered
-- by a "typical" zoom 14 tile. e.g.
-- 14/3403/6198:
-- latT=40.078071, lonL=-105.227051,
-- latB=40.061257, lonR=-105.205078,
-- area=0.000369
.print 'PROCESS tbl_rels_center'
INSERT OR IGNORE INTO tbl_rels_center (rid)
	SELECT rid
		FROM tbl_rels_polygon
		JOIN tbl_rels_range USING (rid)
		WHERE (0.5*(latT-latB)*(lonR-lonL))>0.000369;

-- delete way members for centered relations
.print 'PROCESS tbl_ways_members'
DELETE FROM tbl_ways_members WHERE EXISTS
	( SELECT rid FROM tbl_rels_center WHERE
		tbl_ways_members.rid=tbl_rels_center.rid );

-- insert nodes transitively selected from relations
.print 'PROCESS tbl_nodes_selected'
INSERT OR IGNORE INTO tbl_nodes_selected (nid)
	SELECT nid FROM tbl_nodes_members;

-- insert ways transitively selected from relations
.print 'PROCESS tbl_ways_selected'
INSERT OR IGNORE INTO tbl_ways_selected (wid)
	SELECT wid FROM tbl_ways_members;

-- delete ways which were not selected by during the
-- construction phase or were not transitively selected
-- from relations
.print 'PROCESS tbl_ways'
DELETE FROM tbl_ways WHERE NOT EXISTS
	( SELECT wid FROM tbl_ways_selected WHERE
		tbl_ways_selected.wid=tbl_ways.wid );
.print 'PROCESS tbl_ways_range'
DELETE FROM tbl_ways_range WHERE NOT EXISTS
	( SELECT wid FROM tbl_ways_selected WHERE
		tbl_ways_selected.wid=tbl_ways_range.wid );

/*
 * PROCESS WAYS
 */

-- center large polygon ways
.print 'PROCESS tbl_ways_center'
INSERT OR IGNORE INTO tbl_ways_center (wid)
	SELECT wid
		FROM tbl_ways_polygon
		JOIN tbl_ways_range USING (wid)
		WHERE (0.5*(latT-latB)*(lonR-lonL))>0.000369;

-- delete way nds for centered ways
.print 'PROCESS tbl_ways_nds'
DELETE FROM tbl_ways_nds WHERE EXISTS
	( SELECT wid FROM tbl_ways_center WHERE
		tbl_ways_nds.wid=tbl_ways_center.wid );

-- insert nodes transitively selected from ways
.print 'PROCESS tbl_nodes_selected'
INSERT OR IGNORE INTO tbl_nodes_selected (nid)
	SELECT nid FROM tbl_ways_nds;

/*
 * PROCESS NODES
 */

-- delete nodes which were not selected by during the
-- construction phase or were not transitively selected
-- from ways/relations
.print 'PROCESS tbl_nodes_coords'
DELETE FROM tbl_nodes_coords WHERE NOT EXISTS
	( SELECT nid FROM tbl_nodes_selected WHERE
		tbl_nodes_selected.nid=tbl_nodes_coords.nid );

/*
 * CREATE COORD INDEXES
 * These indexes optimize searches for coordinates. See
 * EXPLAIN QUERY PLAN to verify if the indexes are used.
 * e.g. SELECT * FROM tbl_nodes_coords WHERE lat>0 AND lon>0;
 */

.print 'CREATE idx_nodes_coords'
CREATE INDEX idx_nodes_coords ON tbl_nodes_coords (lat, lon);
.print 'CREATE idx_ways_range_coords'
CREATE INDEX idx_ways_range_coords ON tbl_ways_range (latT, lonL, latB, lonR);
.print 'CREATE idx_rels_range_coords'
CREATE INDEX idx_rels_range_coords ON tbl_rels_range (latT, lonL, latB, lonR);

/*
 * CLEAN UP
 */

.print 'CLEANUP'

-- drop working tables
DROP TABLE tbl_nodes_selected;
DROP TABLE tbl_ways_selected;
DROP TABLE tbl_ways_center;
DROP TABLE tbl_rels_center;
DROP TABLE tbl_ways_polygon;
DROP TABLE tbl_rels_polygon;

-- optimize database for read-only access
VACUUM;

.print 'DONE'

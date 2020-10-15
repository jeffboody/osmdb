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
	center   INTEGER,
	polygon  INTEGER,
	selected INTEGER,
	min_zoom INTEGER
);

CREATE TABLE tbl_rels
(
	rid      INTEGER PRIMARY KEY NOT NULL,
	class    INTEGER,
	name     TEXT,
	abrev    TEXT,
	center   INTEGER,
	polygon  INTEGER,
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
 * CREATE RANGE TABLES
 */

.print 'CREATE RANGE TABLES'

CREATE VIRTUAL TABLE tbl_nodes_range USING rtree
(
	nid,
	lonL,
	lonR,
	latB,
	latT
);

CREATE VIRTUAL TABLE tbl_ways_range USING rtree
(
	wid,
	lonL,
	lonR,
	latB,
	latT
);

CREATE VIRTUAL TABLE tbl_rels_range USING rtree
(
	rid,
	lonL,
	lonR,
	latB,
	latT
);

/*
 * IMPORT TABLES
 */

.stats on
.timer on
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

-- compute the range of info nodes
.print 'INSERT INTO tbl_nodes_range'
INSERT INTO tbl_nodes_range (nid, lonL, lonR, latB, latT)
	SELECT nid, lon, lon, lat, lat
		FROM tbl_nodes_info
		JOIN tbl_nodes_coords USING (nid);

-- compute the range of all ways
.print 'INSERT INTO tbl_ways_range'
INSERT INTO tbl_ways_range (wid, lonL, lonR, latB, latT)
	SELECT wid, min(lon), max(lon), min(lat), max(lat)
		FROM tbl_ways_nds
		JOIN tbl_nodes_coords USING (nid)
		GROUP BY wid;

-- compute the range of all relations
.print 'INSERT INTO tbl_rels_range'
INSERT INTO tbl_rels_range (rid, lonL, lonR, latB, latT)
	SELECT rid, min(lonL), max(lonR), min(latB), max(latT)
		FROM tbl_ways_members
		JOIN tbl_ways_range USING (wid)
		GROUP BY rid;

/*
 * FINISH
 */

.print 'DONE'

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

-- MAIN TABLES

CREATE TABLE tbl_nodes
(
	nid      INTEGER PRIMARY KEY NOT NULL,
	class    INTEGER,
	lat      FLOAT,
	lon      FLOAT,
	name     TEXT,
	abrev    TEXT,
	ele      INTEGER,
	st       INTEGER
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
	cutting  INTEGER
);

CREATE TABLE tbl_rels
(
	rid   INTEGER PRIMARY KEY NOT NULL,
	class INTEGER,
	name  TEXT,
	abrev TEXT
);

CREATE TABLE tbl_ways_nds
(
	idx INTEGER,
	wid INTEGER REFERENCES tbl_ways,
	nid INTEGER REFERENCES tbl_nodes
);

CREATE TABLE tbl_nodes_members
(
	rid  INTEGER REFERENCES tbl_rels,
	nid  INTEGER REFERENCES tbl_nodes,
	role INTEGER
);

CREATE TABLE tbl_ways_members
(
	idx  INTEGER,
	rid  INTEGER REFERENCES tbl_rels,
	wid  INTEGER REFERENCES tbl_ways,
	role INTEGER
);

-- DERIVED TABLES

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

-- WORKING SELECTED TABLES
-- These tables contain references to all nodes/ways which
-- were selected during the initial construction phase and
-- those which were transitively selected by relations or
-- ways.

CREATE TABLE tbl_nodes_selected
(
	nid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_nodes
);

CREATE TABLE tbl_ways_selected
(
	wid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways
);

-- WORKING CENTER TABLES
-- These tables contain references to determine which
-- ways can discard their nds and which relations can
-- discard their member ways

CREATE TABLE tbl_ways_center
(
	wid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways
);

CREATE TABLE tbl_rels_center
(
	rid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_rels
);

-- WORKING POLYGON TABLES
-- These tables contain references to determine which
-- ways and relations are drawn as polygons.

CREATE TABLE tbl_ways_polygon
(
	wid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_ways
);

CREATE TABLE tbl_rels_polygon
(
	rid INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_rels
);

-- COORD INDEXES
-- These indexes optimize searches for coordinates. See
-- EXPLAIN QUERY PLAN to verify if the indexes are used.
-- e.g. SELECT * FROM tbl_nodes WHERE lat>0 AND lon>0;

CREATE INDEX idx_nodes_coords ON tbl_nodes (lat, lon);
CREATE INDEX idx_ways_range_coords ON tbl_ways_range (latT, lonL, latB, lonR);
CREATE INDEX idx_rels_range_coords ON tbl_rels_range (latT, lonL, latB, lonR);

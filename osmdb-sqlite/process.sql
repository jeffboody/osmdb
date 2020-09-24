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

/*
 * PROCESS RELATIONS
 */

-- compute the range of all relations
INSERT INTO tbl_rels_range (rid, latT, lonL, latB, lonR)
	SELECT rid, max(lat) AS latT, min(lon) AS lonL, min(lat) AS latB, max(lon) AS lonR
		FROM tbl_rels
		JOIN tbl_ways_members USING (rid)
		JOIN tbl_ways_nds USING (wid)
		JOIN tbl_nodes USING (nid)
		GROUP BY rid;

-- center large polygon relations
-- large areas are defined to be 50% of the area covered
-- by a "typical" zoom 14 tile. e.g.
-- 14/3403/6198:
-- latT=40.078071, lonL=-105.227051,
-- latB=40.061257, lonR=-105.205078,
-- area=0.000369
INSERT OR IGNORE INTO tbl_rels_center (rid)
	SELECT rid
		FROM tbl_rels_polygon
		JOIN tbl_rels_range USING (rid)
		WHERE (0.5*(latT-latB)*(lonR-lonL))>0.000369;

-- delete way members for centered relations
DELETE FROM tbl_ways_members WHERE EXISTS
	( SELECT * FROM tbl_rels_center WHERE
		tbl_ways_members.rid=tbl_rels_center.rid );

-- insert nodes transitively selected from relations
INSERT OR IGNORE INTO tbl_nodes_selected (nid)
	SELECT nid
		FROM tbl_rels
		JOIN tbl_nodes_members USING (rid);

-- insert ways transitively selected from relations
INSERT OR IGNORE INTO tbl_ways_selected (wid)
	SELECT wid
		FROM tbl_rels
		JOIN tbl_ways_members USING (rid);

/*
 * PROCESS WAYS
 */

-- delete ways which were not selected by during the
-- construction phase or were not transitively selected
-- from relations
DELETE FROM tbl_ways WHERE NOT EXISTS
	( SELECT * FROM tbl_ways_selected WHERE
		tbl_ways_selected.wid=tbl_ways.wid );

-- compute the range of all selected ways
INSERT INTO tbl_ways_range (wid, latT, lonL, latB, lonR)
	SELECT wid, max(lat) AS latT, min(lon) AS lonL, min(lat) AS latB, max(lon) AS lonR
		FROM tbl_ways_selected
		JOIN tbl_ways_nds USING (wid)
		JOIN tbl_nodes USING (nid)
		GROUP BY wid;

-- center large polygon ways
INSERT OR IGNORE INTO tbl_ways_center (wid)
	SELECT wid
		FROM tbl_ways_polygon
		JOIN tbl_ways_range USING (wid)
		WHERE (0.5*(latT-latB)*(lonR-lonL))>0.000369;

-- delete way nds for centered ways
DELETE FROM tbl_ways_nds WHERE EXISTS
	( SELECT * FROM tbl_ways_center WHERE
		tbl_ways_nds.wid=tbl_ways_center.wid );

-- insert nodes transitively selected from ways
INSERT OR IGNORE INTO tbl_nodes_selected (nid)
	SELECT nid
		FROM tbl_ways
		JOIN tbl_ways_nds USING (wid);

/*
 * PROCESS NODES
 */

-- delete nodes which were not selected by during the
-- construction phase or were not transitively selected
-- from ways/relations
DELETE FROM tbl_nodes WHERE NOT EXISTS
	( SELECT * FROM tbl_nodes_selected WHERE
		tbl_nodes_selected.nid=tbl_nodes.nid );

/*
 * CLEAN UP
 */

-- drop working tables
DROP TABLE tbl_nodes_selected;
DROP TABLE tbl_ways_selected;
DROP TABLE tbl_ways_center;
DROP TABLE tbl_rels_center;

-- optimize database for read-only access
VACUUM;

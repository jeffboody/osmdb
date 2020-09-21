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

-- compute the range of all relations
INSERT INTO tbl_rels_range (rid, latT, lonL, latB, lonR)
	SELECT rid, max(lat) AS latT, min(lon) AS lonL, min(lat) AS latB, max(lon) AS lonR
		FROM tbl_rels AS a
		JOIN tbl_ways_members AS b USING (rid)
		JOIN tbl_ways_nds AS c USING (wid)
		JOIN tbl_nodes AS d USING (nid)
		GROUP BY a.rid;

-- insert nodes transitively selected from relations
INSERT OR IGNORE INTO tbl_nodes_selected (nid)
	SELECT nid
		FROM tbl_rels AS a
		JOIN tbl_nodes_members AS b USING (rid);

-- insert ways transitively selected from relations
-- ignoring the ways from centered relations
INSERT OR IGNORE INTO tbl_ways_selected (wid)
	SELECT wid
		FROM tbl_rels AS a
		LEFT OUTER JOIN tbl_rels_center AS b USING (rid)
		JOIN tbl_ways_members AS c USING (rid)
		WHERE b.center IS NULL;

-- delete ways which were not selected by during the
-- construction phase or were not transitively selected
-- from relations
DELETE FROM tbl_ways WHERE NOT EXISTS
	( SELECT * FROM tbl_ways_selected WHERE
		tbl_ways_selected.wid=tbl_ways.wid );

-- compute the range of all selected ways
INSERT INTO tbl_ways_range (wid, latT, lonL, latB, lonR)
	SELECT wid, max(lat) AS latT, min(lon) AS lonL, min(lat) AS latB, max(lon) AS lonR
		FROM tbl_ways_selected AS a
		JOIN tbl_ways_nds AS b USING (wid)
		JOIN tbl_nodes AS c USING (nid)
		GROUP BY a.wid;

-- insert nodes transitively selected from ways
-- ignoring the nodes from centered ways
INSERT OR IGNORE INTO tbl_nodes_selected (nid)
	SELECT nid
		FROM tbl_ways AS a
		LEFT OUTER JOIN tbl_ways_center AS b USING (wid)
		JOIN tbl_ways_nds AS c USING (wid)
		WHERE b.center IS NULL;

-- delete nodes which were not selected by during the
-- construction phase or were not transitively selected
-- from ways/relations
DELETE FROM tbl_nodes WHERE NOT EXISTS
	( SELECT * FROM tbl_nodes_selected WHERE
		tbl_nodes_selected.nid=tbl_nodes.nid );

-- drop working tables
DROP TABLE tbl_nodes_selected;
DROP TABLE tbl_ways_selected;
DROP TABLE tbl_ways_center;
DROP TABLE tbl_rels_center;

-- optimize database for read-only access
VACUUM;

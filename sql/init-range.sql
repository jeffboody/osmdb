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
PRAGMA cache_size = 100000;

.stats on
.timer on

/*
 * INIT RANGE
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

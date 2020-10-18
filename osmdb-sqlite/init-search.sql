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
 * LOAD MODULES
 */

.load ./spellfix.so

/*
 * CREATE TABLES
 */

CREATE VIRTUAL TABLE tbl_nodes_text USING fts4(nid, txt);
CREATE VIRTUAL TABLE tbl_nodes_aux  USING fts4aux(tbl_nodes_text);
CREATE VIRTUAL TABLE tbl_ways_text  USING fts4(wid, txt);
CREATE VIRTUAL TABLE tbl_ways_aux   USING fts4aux(tbl_ways_text);
CREATE VIRTUAL TABLE tbl_rels_text  USING fts4(rid, txt);
CREATE VIRTUAL TABLE tbl_rels_aux   USING fts4aux(tbl_rels_text);
CREATE VIRTUAL TABLE tbl_spellfix   USING spellfix1;

/*
 * IMPORT TABLES
 */

.mode csv
.separator |
.print 'IMPORT tbl_nodes_text'
.import tbl_nodes_text.data tbl_nodes_text
.print 'IMPORT tbl_ways_text'
.import tbl_ways_text.data tbl_ways_text
.print 'IMPORT tbl_rels_text'
.import tbl_rels_text.data tbl_rels_text

/*
 * SPELLFIX
 */

.print 'INSERT INTO tbl_spellfix (nodes)'
INSERT INTO tbl_spellfix(word)
	SELECT term FROM tbl_nodes_aux WHERE col='*';
.print 'INSERT INTO tbl_spellfix (ways)'
INSERT INTO tbl_spellfix(word)
	SELECT term FROM tbl_ways_aux WHERE col='*';
.print 'INSERT INTO tbl_spellfix (rels)'
INSERT INTO tbl_spellfix(word)
	SELECT term FROM tbl_rels_aux WHERE col='*';

/*
 * FINISH
 */

-- drop temporary aux tables
.print 'DROP tbl_rels_aux'
DROP TABLE tbl_rels_aux;
.print 'DROP tbl_ways_aux'
DROP TABLE tbl_ways_aux;
.print 'DROP tbl_nodes_aux'
DROP TABLE tbl_nodes_aux;

.print 'DONE'

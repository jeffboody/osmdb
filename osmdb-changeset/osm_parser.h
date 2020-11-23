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

#ifndef osm_parser_H
#define osm_parser_H

#include "libsqlite3/sqlite3.h"

typedef struct
{
	int    state;
	int    depth;
	double change_id;

	sqlite3* db;

	// statements
	sqlite3_stmt* stmt_create_ways_range;
	sqlite3_stmt* stmt_create_rels_range;
	sqlite3_stmt* stmt_drop_ways_range;
	sqlite3_stmt* stmt_drop_rels_range;
	sqlite3_stmt* stmt_insert_ways_range;
	sqlite3_stmt* stmt_insert_rels_range;
	sqlite3_stmt* stmt_delete_ways_range;
	sqlite3_stmt* stmt_delete_rels_range;

	// arguments
	int idx_insert_ways_range_latT;
	int idx_insert_ways_range_lonL;
	int idx_insert_ways_range_latB;
	int idx_insert_ways_range_lonR;
	int idx_insert_rels_range_latT;
	int idx_insert_rels_range_lonL;
	int idx_insert_rels_range_latB;
	int idx_insert_rels_range_lonR;

	// attributes
	double attr_change_id;
	double attr_min_lat;
	double attr_min_lon;
	double attr_max_lat;
	double attr_max_lon;
} osm_parser_t;

osm_parser_t* osm_parser_new(double change_id,
                             const char* fname);
void          osm_parser_delete(osm_parser_t** _self);
int           osm_parser_start(void* priv, int line,
                               const char* name,
                               const char** atts);
int           osm_parser_end(void* priv,
                             int line,
                             const char* name,
                             const char* content);
int           osm_parser_finish(osm_parser_t* self);

#endif

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

#ifndef osmdb_database_H
#define osmdb_database_H

#include "libcc/cc_list.h"
#include "libcc/cc_map.h"
#include "libsqlite3/sqlite3.h"
#include "libxmlstream/xml_ostream.h"

typedef struct
{
	sqlite3* db;

	int nthreads;

	// search statements
	sqlite3_stmt** stmt_spellfix;
	sqlite3_stmt** stmt_search_nodes;
	sqlite3_stmt** stmt_search_ways;
	sqlite3_stmt** stmt_search_rels;

	// select statements
	sqlite3_stmt** stmt_select_nodes_range;
	sqlite3_stmt*  stmt_select_node;
	sqlite3_stmt** stmt_select_rels_range;
	sqlite3_stmt*  stmt_select_relation;
	sqlite3_stmt*  stmt_select_mnodes;
	sqlite3_stmt*  stmt_select_mways;
	sqlite3_stmt*  stmt_select_way;
	sqlite3_stmt*  stmt_select_wnds;
	sqlite3_stmt** stmt_select_ways_range;

	// search arguments
	int idx_spellfix_arg;
	int idx_search_nodes_arg;
	int idx_search_ways_arg;
	int idx_search_rels_arg;

	// select arguments
	int idx_select_nodes_range_latT;
	int idx_select_nodes_range_lonL;
	int idx_select_nodes_range_latB;
	int idx_select_nodes_range_lonR;
	int idx_select_nodes_range_zoom;
	int idx_select_node_nid;
	int idx_select_rels_range_latT;
	int idx_select_rels_range_lonL;
	int idx_select_rels_range_latB;
	int idx_select_rels_range_lonR;
	int idx_select_rels_range_zoom;
	int idx_select_relation_rid;
	int idx_select_mnodes_rid;
	int idx_select_mways_rid;
	int idx_select_way_wid;
	int idx_select_wnds_wid;
	int idx_select_ways_range_latT;
	int idx_select_ways_range_lonL;
	int idx_select_ways_range_latB;
	int idx_select_ways_range_lonR;
	int idx_select_ways_range_zoom;

	// select way column indices
	int idx_select_way_name;
	int idx_select_way_abrev;
	int idx_select_way_class;
	int idx_select_way_layer;
	int idx_select_way_oneway;
	int idx_select_way_bridge;
	int idx_select_way_tunnel;
	int idx_select_way_cutting;
	int idx_select_way_center;
	int idx_select_way_latT;
	int idx_select_way_lonL;
	int idx_select_way_latB;
	int idx_select_way_lonR;

	// sampling constants
	float min_dist14;
	float min_dist11;
	float min_dist8;

	// cache protected by object mutex
	cc_list_t*      object_list;
	cc_map_t*       object_map;
	pthread_mutex_t object_mutex;
} osmdb_database_t;

osmdb_database_t* osmdb_database_new(const char* fname,
                                     int nthreads);
void              osmdb_database_delete(osmdb_database_t** _self);
void              osmdb_database_spellfix(osmdb_database_t* self,
                                          int tid,
                                          const char* text,
                                          char* spellfix);
int               osmdb_database_search(osmdb_database_t* self,
                                        int tid,
                                        const char* text,
                                        xml_ostream_t* os);
int               osmdb_database_tile(osmdb_database_t* self,
                                      int tid,
                                      int zoom, int x, int y,
                                      xml_ostream_t* os);

#endif

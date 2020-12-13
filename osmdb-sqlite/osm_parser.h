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

#include <iconv.h>

#include "libcc/cc_list.h"
#include "libcc/cc_map.h"
#include "libsqlite3/sqlite3.h"
#include "../osmdb_style.h"
#include "../osmdb_table.h"

typedef struct
{
	int    type;
	int    role;
	double ref;
} osm_relationMember_t;

typedef struct
{
	int nodes;
	int ways;
	int rels;
} osm_classHistogram_t;

typedef struct
{
	int state;
	int batch_size;

	double t0;
	double t1;

	sqlite3* db;

	// statements
	sqlite3_stmt* stmt_begin;
	sqlite3_stmt* stmt_end;
	sqlite3_stmt* stmt_rollback;
	sqlite3_stmt* stmt_select_rels;
	sqlite3_stmt* stmt_select_rels_range;
	sqlite3_stmt* stmt_insert_class_rank;
	sqlite3_stmt* stmt_insert_nodes_coords;
	sqlite3_stmt* stmt_insert_nodes_info;
	sqlite3_stmt* stmt_insert_ways;
	sqlite3_stmt* stmt_insert_rels;
	sqlite3_stmt* stmt_insert_nodes_members;
	sqlite3_stmt* stmt_insert_ways_members;
	sqlite3_stmt* stmt_insert_nodes_range;
	sqlite3_stmt* stmt_insert_ways_range;
	sqlite3_stmt* stmt_insert_rels_range;
	sqlite3_stmt* stmt_insert_nodes_text;
	sqlite3_stmt* stmt_insert_ways_text;
	sqlite3_stmt* stmt_insert_rels_text;

	// indices
	int idx_select_rels_range_rid;
	int idx_insert_class_rank_class;
	int idx_insert_class_rank_rank;
	int idx_insert_nodes_coords_nid;
	int idx_insert_nodes_coords_lat;
	int idx_insert_nodes_coords_lon;
	int idx_insert_nodes_info_nid;
	int idx_insert_nodes_info_class;
	int idx_insert_nodes_info_name;
	int idx_insert_nodes_info_abrev;
	int idx_insert_nodes_info_ele;
	int idx_insert_nodes_info_st;
	int idx_insert_nodes_info_min_zoom;
	int idx_insert_ways_wid;
	int idx_insert_ways_class;
	int idx_insert_ways_layer;
	int idx_insert_ways_name;
	int idx_insert_ways_abrev;
	int idx_insert_ways_oneway;
	int idx_insert_ways_bridge;
	int idx_insert_ways_tunnel;
	int idx_insert_ways_cutting;
	int idx_insert_ways_center;
	int idx_insert_ways_polygon;
	int idx_insert_ways_selected;
	int idx_insert_ways_min_zoom;
	int idx_insert_ways_nds;
	int idx_insert_rels_rid;
	int idx_insert_rels_class;
	int idx_insert_rels_name;
	int idx_insert_rels_abrev;
	int idx_insert_rels_center;
	int idx_insert_rels_polygon;
	int idx_insert_rels_min_zoom;
	int idx_insert_nodes_members_rid;
	int idx_insert_nodes_members_nid;
	int idx_insert_nodes_members_role;
	int idx_insert_ways_members_idx;
	int idx_insert_ways_members_rid;
	int idx_insert_ways_members_wid;
	int idx_insert_ways_members_role;
	int idx_insert_nodes_range_nid;
	int idx_insert_nodes_range_l;
	int idx_insert_nodes_range_r;
	int idx_insert_nodes_range_b;
	int idx_insert_nodes_range_t;
	int idx_insert_ways_range_wid;
	int idx_insert_ways_range_l;
	int idx_insert_ways_range_r;
	int idx_insert_ways_range_b;
	int idx_insert_ways_range_t;
	int idx_insert_rels_range_rid;
	int idx_insert_rels_range_l;
	int idx_insert_rels_range_r;
	int idx_insert_rels_range_b;
	int idx_insert_rels_range_t;
	int idx_insert_nodes_text_nid;
	int idx_insert_nodes_text_txt;
	int idx_insert_ways_text_wid;
	int idx_insert_ways_text_txt;
	int idx_insert_rels_text_rid;
	int idx_insert_rels_text_txt;

	osmdb_style_t* style;

	// attributes
	double attr_id;
	double attr_lat;
	double attr_lon;

	// english flag
	int name_en;

	// tags
	char tag_name[256];
	char tag_abrev[256];
	char tag_text[256];
	int  tag_ele;
	int  tag_st;
	int  tag_class;
	int  tag_way_layer;
	int  tag_way_oneway;
	int  tag_way_bridge;
	int  tag_way_tunnel;
	int  tag_way_cutting;

	// type used for relations
	int rel_type;

	// way nds
	int     ways_nds_idx;
	int     ways_nds_count;
	double* ways_nds_array;

	// rel members
	cc_list_t* rel_members;

	// class constants
	int class_none;
	int building_yes;
	int barrier_yes;
	int office_yes;
	int historic_yes;
	int man_made_yes;
	int tourism_yes;

	// histogram of class types
	double stats_nodes;
	double stats_ways;
	double stats_relations;
	osm_classHistogram_t* histogram;

	// class name/code map
	cc_map_t* class_map;

	// UTF-8 to ASCII conversion
	iconv_t cd;

	// page table/cache for node tiles
	osmdb_table_t* page_table;
	cc_list_t*     page_list;
	cc_map_t*      page_map;
} osm_parser_t;

osm_parser_t* osm_parser_new(const char* style,
                             const char* db_name,
                             const char* tbl_name);
void          osm_parser_delete(osm_parser_t** _self);
int           osm_parser_beginTransaction(osm_parser_t* self);
int           osm_parser_endTransaction(osm_parser_t* self);
void          osm_parser_rollbackTransaction(osm_parser_t* self);
int           osm_parser_createTables(osm_parser_t* self);
int           osm_parser_createIndices(osm_parser_t* self);
int           osm_parser_initClassRank(osm_parser_t* self);
int           osm_parser_initRange(osm_parser_t* self);
int           osm_parser_initSearch(osm_parser_t* self);
int           osm_parser_dropAuxTables(osm_parser_t* self);
int           osm_parser_parseFile(osm_parser_t* self,
                                   const char* fname);
int           osm_parser_start(void* priv, int line,
                               float progress,
                               const char* name,
                               const char** atts);
int           osm_parser_end(void* priv,
                             int line,
                             float progress,
                             const char* name,
                             const char* content);

#endif

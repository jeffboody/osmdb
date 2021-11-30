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
#include "osmdb/index/osmdb_index.h"
#include "osmdb/osmdb_style.h"

typedef struct
{
	int state;

	uint64_t count_nodes;
	uint64_t count_ways;
	uint64_t count_rels;

	double t0;
	double t1;

	osmdb_index_t* index;

	osmdb_style_t* style;

	// parsing data
	int way_nds_maxCount;
	int rel_members_maxCount;
	osmdb_nodeCoord_t*  node_coord;
	osmdb_nodeInfo_t*   node_info;
	osmdb_wayInfo_t*    way_info;
	osmdb_wayRange_t*   way_range;
	osmdb_wayNds_t*     way_nds;
	osmdb_relInfo_t*    rel_info;
	osmdb_relRange_t*   rel_range;
	osmdb_relMembers_t* rel_members;

	// english flag
	int name_en;

	// tags
	int64_t tag_changeset;
	char    tag_name[256];
	char    tag_abrev[256];
	char    tag_ref[256];
	int     tag_highway;

	// class constants
	int class_none;
	int building_yes;
	int barrier_yes;
	int office_yes;
	int historic_yes;
	int man_made_yes;
	int tourism_yes;
	int highway_motorway;
	int highway_junction;

	// relation member constants
	int rel_member_type_node;
	int rel_member_type_way;
	int rel_member_role_inner;
	int rel_member_role_admin_centre;
	int rel_member_role_label;

	// class name/code map
	cc_map_t* class_map;

	// ignore capitolization map
	cc_map_t* nocaps_map;

	// abreviation map
	cc_map_t* abrev_map;

	// UTF-8 to ASCII conversion
	iconv_t cd;
} osm_parser_t;

osm_parser_t* osm_parser_new(const char* style,
                             const char* db_name);
void          osm_parser_delete(osm_parser_t** _self);
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

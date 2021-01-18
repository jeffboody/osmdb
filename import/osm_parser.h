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

	// blobs
	int way_nds_maxCount;
	int rel_members_maxCount;
	osmdb_blobNodeCoord_t*  node_coord;
	osmdb_blobNodeInfo_t*   node_info;
	osmdb_blobWayInfo_t*    way_info;
	osmdb_blobWayRange_t*   way_range;
	osmdb_blobWayNds_t*     way_nds;
	osmdb_blobRelInfo_t*    rel_info;
	osmdb_blobRelRange_t*   rel_range;
	osmdb_blobRelMembers_t* rel_members;

	// english flag
	int name_en;

	// tags
	int64_t tag_changeset;
	char    tag_name[256];
	char    tag_abrev[256];

	// class constants
	int class_none;
	int building_yes;
	int barrier_yes;
	int office_yes;
	int historic_yes;
	int man_made_yes;
	int tourism_yes;

	// class name/code map
	cc_map_t* class_map;

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

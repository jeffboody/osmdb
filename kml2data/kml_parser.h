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

#ifndef kml_parser_H
#define kml_parser_H

#include <stdio.h>

#include "libcc/cc_list.h"
#include "libcc/cc_map.h"

typedef struct
{
	double id;
	double lat;
	double lon;
} kml_node_t;

typedef struct
{
	// parse state
	double     nid;
	double     wid;
	char       name[256];
	int        class;
	int        simpledata;
	int        discard;
	cc_list_t* list_state;
	int        way_nds;
	int        seg_nds;

	// bounding box
	double way_latT;
	double way_lonL;
	double way_latB;
	double way_lonR;
	double seg_latT;
	double seg_lonL;
	double seg_latB;
	double seg_lonR;

	// node data
	cc_map_t* map_nodes;

	// output streams
	FILE* tbl_nodes_coords;
	FILE* tbl_nodes_info;
	FILE* tbl_ways;
	FILE* tbl_ways_range;
	FILE* tbl_ways_nds;
} kml_parser_t;

kml_parser_t* kml_parser_new(void);
void          kml_parser_delete(kml_parser_t** _self);
int           kml_parser_parse(kml_parser_t* self,
                               const char* fname_kml);
void          kml_parser_finish(kml_parser_t* self);

#endif

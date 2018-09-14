/*
 * Copyright (c) 2018 Jeff Boody
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

#include "a3d/a3d_list.h"

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
	xml_ostream_t* os;

	// attributes
	double attr_id;
	double attr_lat;
	double attr_lon;

	// tags
	char tag_name[256];
	char tag_abrev[256];
	int  tag_ele;
	int  tag_st;
	int  tag_class;

	// type used for relations
	int rel_type;

	// way nds
	a3d_list_t* way_nds;

	// rel members
	a3d_list_t* rel_members;

	// histogram of class types
	double stats_nodes;
	double stats_ways;
	double stats_relations;
	osm_classHistogram_t* histogram;
} osm_parser_t;

osm_parser_t* osm_parser_new(xml_ostream_t* os);
void          osm_parser_delete(osm_parser_t** _self);
int           osm_parser_start(void* priv, int line,
                               const char* name,
                               const char** atts);
int           osm_parser_end(void* priv,
                             int line,
                             const char* name,
                             const char* content);

#endif

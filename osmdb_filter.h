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

#ifndef osmdb_filter_H
#define osmdb_filter_H

#include "../a3d/a3d_hashmap.h"
#include "../a3d/a3d_list.h"
#include "osmdb_node.h"
#include "osmdb_way.h"
#include "osmdb_relation.h"

typedef struct
{
	int zoom;
	int center;
	int named;
} osmdb_filterInfo_t;

typedef struct
{
	// map from class name to filter info
	a3d_hashmap_t* info;
} osmdb_filter_t;

osmdb_filter_t*     osmdb_filter_new(const char* fname);
void                osmdb_filter_delete(osmdb_filter_t** _self);
osmdb_filterInfo_t* osmdb_filter_selectNode(osmdb_filter_t* self,
                                            osmdb_node_t* node);
osmdb_filterInfo_t* osmdb_filter_selectWay(osmdb_filter_t* self,
                                           osmdb_way_t* way);
osmdb_filterInfo_t* osmdb_filter_selectRelation(osmdb_filter_t* self,
                                                osmdb_relation_t* relation);

#endif

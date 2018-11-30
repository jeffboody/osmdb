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

#ifndef osmdb_range_H
#define osmdb_range_H

#include "osmdb_node.h"
#include "osmdb_way.h"
#include "osmdb_relation.h"

struct osmdb_index_s;

typedef struct
{
	int    pts;
	double latT;
	double lonL;
	double latB;
	double lonR;
} osmdb_range_t;

void osmdb_range_init(osmdb_range_t* self);
void osmdb_range_addPt(osmdb_range_t* self,
                       double lat, double lon);
void osmdb_range_addNode(osmdb_range_t* self,
                         osmdb_node_t* node);
void osmdb_range_addWay(osmdb_range_t* self,
                        struct osmdb_index_s* index,
                        osmdb_way_t* way);
void osmdb_range_addRelation(osmdb_range_t* self,
                             struct osmdb_index_s* index,
                             osmdb_relation_t* relation);

#endif

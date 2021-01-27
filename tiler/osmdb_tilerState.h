/*
 * Copyright (c) 2021 Jeff Boody
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

#ifndef osmdb_tilerState_H
#define osmdb_tilerState_H

#include "libcc/cc_list.h"
#include "libcc/cc_map.h"
#include "libcc/cc_multimap.h"
#include "../index/osmdb_index.h"
#include "osmdb_ostream.h"

typedef struct
{
	int zoom;
	int x;
	int y;

	double latT;
	double lonL;
	double latB;
	double lonR;

	float min_dist;

	osmdb_ostream_t* os;
	cc_map_t*        map_export;
	cc_map_t*        map_segs;
	cc_multimap_t*   mm_nds_join;
	cc_list_t*       list_nds;
} osmdb_tilerState_t;

osmdb_tilerState_t* osmdb_tilerState_new(void);
void                osmdb_tilerState_delete(osmdb_tilerState_t** _self);
int                 osmdb_tilerState_init(osmdb_tilerState_t* self,
                                          int zoom, int x, int y);
void                osmdb_tilerState_reset(osmdb_tilerState_t* self,
                                           osmdb_index_t* index);

#endif

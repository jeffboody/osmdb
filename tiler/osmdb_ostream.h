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

#ifndef osmdb_ostream_H
#define osmdb_ostream_H

#include <stdint.h>

#include "../index/osmdb_type.h"
#include "osmdb_tile.h"

typedef struct
{
	size_t size;
	size_t offset;
	size_t offset_rel;
	size_t offset_way;
	void*  data;

	// bounding tile rect
	float tileT;
	float tileL;
	float tileB;
	float tileR;
} osmdb_ostream_t;

osmdb_ostream_t* osmdb_ostream_new(void);
void             osmdb_ostream_delete(osmdb_ostream_t** _self);
int              osmdb_ostream_beginTile(osmdb_ostream_t* self,
                                         int zoom, int x, int y,
                                         int64_t changeset);
osmdb_tile_t*    osmdb_ostream_endTile(osmdb_ostream_t* self,
                                       size_t* _size);
int              osmdb_ostream_beginRel(osmdb_ostream_t* self,
                                        osmdb_relInfo_t* rel_info,
                                        osmdb_relRange_t* rel_range,
                                        int size_name,
                                        const char* name,
                                        osmdb_nodeCoord_t* node_coord);
void             osmdb_ostream_endRel(osmdb_ostream_t* self);
int              osmdb_ostream_beginWay(osmdb_ostream_t* self,
                                        osmdb_wayInfo_t* way_info,
                                        osmdb_wayRange_t* way_range,
                                        int flags);
int              osmdb_ostream_addWayCoord(osmdb_ostream_t* self,
                                           osmdb_nodeCoord_t* node_coord);
void             osmdb_ostream_endWay(osmdb_ostream_t* self);
int              osmdb_ostream_addNode(osmdb_ostream_t* self,
                                       osmdb_nodeInfo_t* node_info,
                                       osmdb_nodeCoord_t* node_coord);

#endif

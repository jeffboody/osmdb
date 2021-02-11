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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "terrain/terrain_util.h"
#include "osmdb_ostream.h"

/***********************************************************
* private                                                  *
***********************************************************/

static void osmdb_ostream_reset(osmdb_ostream_t* self)
{
	ASSERT(self);

	FREE(self->data);
	memset((void*) self, 0, sizeof(osmdb_ostream_t));
}

static void*
osmdb_ostream_data(osmdb_ostream_t* self, size_t offset)
{
	ASSERT(self);

	if(self->data == NULL)
	{
		return NULL;
	}
	else if(offset >= self->size)
	{
		LOGE("invalid offset=%" PRId64, (int64_t) offset);
		osmdb_ostream_reset(self);
		return NULL;
	}

	char* ptr = (char*) self->data;
	return (void*) &ptr[offset];
}

static void*
osmdb_ostream_add(osmdb_ostream_t* self, size_t size,
                  size_t* _offset)
{
	// _offset may be NULL
	ASSERT(self);

	// osmdb_ostream_add may invalidate pointers returned by
	// osmdb_ostream_add or osmdb_ostream_data

	size_t offset = self->offset;
	size_t resize = offset + size;
	if(self->size < resize)
	{
		size_t tmp_size = self->size;
		while(tmp_size < resize)
		{
			tmp_size += 4096;
		}

		void* tmp = REALLOC(self->data, tmp_size);
		if(tmp == NULL)
		{
			LOGE("REALLOC failed");
			osmdb_ostream_reset(self);
			return NULL;
		}

		self->data = tmp;
		self->size = tmp_size;
	}

	// update next offset
	self->offset += size;

	// optionally return offset
	if(_offset)
	{
		*_offset = offset;
	}

	return osmdb_ostream_data(self, offset);
}

static void
osmdb_ostream_coord2pt(osmdb_ostream_t* self,
                       double lat, double lon,
                       osmdb_point_t* pt)
{
	ASSERT(self);
	ASSERT(pt);

	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) osmdb_ostream_data(self, 0);
	if(tile == NULL)
	{
		return;
	}

	float tileX;
	float tileY;
	terrain_coord2tile(lat, lon, tile->zoom,
	                   &tileX, &tileY);

	// compute the uv coordinates
	// tr = (1.0f, 1.0f)
	// bl = (0.0f, 0.0f)
	float x = (tileX - self->tileL)/
	          (self->tileR - self->tileL);
	float y = (tileY - self->tileB)/
	          (self->tileT - self->tileB);

	// translate to pt coordinates
	// tr = (16383.0f, 16383.0f)
	// bl = (-16384.0f, -16384.0f)
	x = 32767.0f*x - 16384.0f;
	y = 32767.0f*y - 16384.0f;

	// clamp pt coordinates
	// short: -32768 => 32767
	if(x > 32767.0f)
	{
		x = 32767.0f;
	}
	else if(x < -32768.0f)
	{
		x = -32768.0f;
	}

	if(y > 32767.0f)
	{
		y = 32767.0f;
	}
	else if(y < -32768.0f)
	{
		y = -32768.0f;
	}

	// convert to short
	pt->x = (short) x;
	pt->y = (short) y;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_ostream_t* osmdb_ostream_new(void)
{
	osmdb_ostream_t* self;
	self = (osmdb_ostream_t*)
	       CALLOC(1, sizeof(osmdb_ostream_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	return self;
}

void osmdb_ostream_delete(osmdb_ostream_t** _self)
{
	ASSERT(_self);

	osmdb_ostream_t* self = *_self;
	if(self)
	{
		FREE(self->data);
		FREE(self);
		*_self = NULL;
	}
}

int osmdb_ostream_beginTile(osmdb_ostream_t* self,
                            int zoom, int x, int y,
                            int64_t changeset)
{
	ASSERT(self);

	// clear out previous tile if it exists
	osmdb_ostream_reset(self);

	// initialize tile
	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*)
	       osmdb_ostream_add(self, sizeof(osmdb_tile_t),
	                         NULL);
	if(tile == NULL)
	{
		return 0;
	}
	memset((void*) tile, 0, sizeof(osmdb_tile_t));

	tile->magic     = OSMDB_TILE_MAGIC;
	tile->version   = OSMDB_TILE_VERSION;
	tile->zoom      = zoom;
	tile->x         = x;
	tile->y         = y;
	tile->changeset = changeset;

	double latT;
	double lonL;
	double latB;
	double lonR;
	terrain_bounds(x, y, zoom, &latT, &lonL, &latB, &lonR);
	terrain_coord2tile(latT, lonL, zoom,
	                   &self->tileL, &self->tileT);
	terrain_coord2tile(latB, lonR, zoom,
	                   &self->tileR, &self->tileB);

	return 1;
}

osmdb_tile_t*
osmdb_ostream_endTile(osmdb_ostream_t* self,
                      size_t* _size)
{
	// _size may be NULL
	ASSERT(self);

	// get tile data
	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) self->data;
	if(_size)
	{
		// size is the allocation size and
		// offset is the tile size
		*_size = self->offset;
	}

	// take ownership of data
	self->data = NULL;
	osmdb_ostream_reset(self);

	return tile;
}

int osmdb_ostream_beginRel(osmdb_ostream_t* self,
                           osmdb_relInfo_t* rel_info,
                           osmdb_relRange_t* rel_range,
                           int size_name, const char* name,
                           osmdb_nodeCoord_t* node_coord)
{
	// name and node_coord may be NULL
	ASSERT(self);
	ASSERT(rel_info);
	ASSERT(rel_range);

	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) osmdb_ostream_data(self, 0);
	if(tile == NULL)
	{
		return 0;
	}

	// elements must be added in order of rel/way/node
	if((tile->count_ways  > tile->count_rels) ||
	   (tile->count_nodes > tile->count_rels))
	{
		LOGE("invalid count: %i/%i/%i",
		     tile->count_rels, tile->count_ways,
		     tile->count_nodes);
		osmdb_ostream_reset(self);
		return 0;
	}

	// add rel
	osmdb_rel_t* rel;
	size_t       offset_rel;
	rel = (osmdb_rel_t*)
	      osmdb_ostream_add(self, sizeof(osmdb_rel_t),
	                        &offset_rel);
	if(rel == NULL)
	{
		return 0;
	}
	rel->type  = rel_info->type;
	rel->class = rel_info->class;
	rel->count = 0;

	// initalize center
	double latT = rel_range->latT;
	double lonL = rel_range->lonL;
	double latB = rel_range->latB;
	double lonR = rel_range->lonR;
	double lat = latB + (latT - latB)/2.0;
	double lon = lonL + (lonR - lonL)/2.0;
	if(node_coord)
	{
		lat = node_coord->lat;
		lon = node_coord->lon;
	}
	osmdb_ostream_coord2pt(self, lat, lon, &rel->center);

	// initialize range
	osmdb_point_t tl = { 0 };
	osmdb_point_t br = { 0 };
	osmdb_ostream_coord2pt(self, latT, lonL, &tl);
	osmdb_ostream_coord2pt(self, latB, lonR, &br);
	rel->range.t = tl.y;
	rel->range.l = tl.x;
	rel->range.b = br.y;
	rel->range.r = br.x;

	// initialize name
	if(name)
	{
		void* ptr;
		ptr = osmdb_ostream_add(self, size_name, NULL);
		if(ptr == NULL)
		{
			return 0;
		}

		// restore rel in case data was reallocated
		rel = (osmdb_rel_t*)
		      osmdb_ostream_data(self, offset_rel);

		memcpy(ptr, (const void*) name, (size_t) size_name);
	}
	rel->size_name   = size_name;
	self->offset_rel = offset_rel;

	return 1;
}

void osmdb_ostream_endRel(osmdb_ostream_t* self)
{
	ASSERT(self);

	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) osmdb_ostream_data(self, 0);
	if(tile == NULL)
	{
		return;
	}

	++tile->count_rels;
	self->offset_rel = 0;
}

int osmdb_ostream_beginWay(osmdb_ostream_t* self,
                           osmdb_wayInfo_t* way_info,
                           osmdb_wayRange_t* way_range,
                           int flags)
{
	// node_info and node_coord may be NULL
	ASSERT(self);
	ASSERT(way_info);
	ASSERT(way_range);

	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) osmdb_ostream_data(self, 0);
	if(tile == NULL)
	{
		return 0;
	}

	// elements must be added in order of rel/way/node
	if(tile->count_nodes > tile->count_ways)
	{
		LOGE("invalid count: %i/%i/%i",
		     tile->count_rels, tile->count_ways,
		     tile->count_nodes);
		osmdb_ostream_reset(self);
		return 0;
	}

	// add way
	osmdb_way_t* way;
	size_t       offset_way;
	way = (osmdb_way_t*)
	      osmdb_ostream_add(self, sizeof(osmdb_way_t),
	                        &offset_way);
	if(way == NULL)
	{
		return 0;
	}
	way->class = way_info->class;
	way->layer = way_info->layer;
	way->flags = way_info->flags | flags;
	way->count = 0;

	// initalize center
	double latT = way_range->latT;
	double lonL = way_range->lonL;
	double latB = way_range->latB;
	double lonR = way_range->lonR;
	double lat = latB + (latT - latB)/2.0;
	double lon = lonL + (lonR - lonL)/2.0;
	osmdb_ostream_coord2pt(self, lat, lon, &way->center);

	// initialize range
	osmdb_point_t tl = { 0 };
	osmdb_point_t br = { 0 };
	osmdb_ostream_coord2pt(self, latT, lonL, &tl);
	osmdb_ostream_coord2pt(self, latB, lonR, &br);
	way->range.t = tl.y;
	way->range.l = tl.x;
	way->range.b = br.y;
	way->range.r = br.x;

	// initialize name
	char* name      = osmdb_wayInfo_name(way_info);
	int   size_name = way_info->size_name;
	if(name)
	{
		void* ptr = osmdb_ostream_add(self, size_name, NULL);
		if(ptr == NULL)
		{
			return 0;
		}

		// restore way in case data was reallocated
		way = (osmdb_way_t*)
		      osmdb_ostream_data(self, offset_way);

		memcpy(ptr, (const void*) name, (size_t) size_name);
	}
	way->size_name   = size_name;
	self->offset_way = offset_way;

	return 1;
}

int osmdb_ostream_addWayCoord(osmdb_ostream_t* self,
                              osmdb_nodeCoord_t* node_coord)
{
	ASSERT(self);
	ASSERT(node_coord);

	if(self->offset_way == 0)
	{
		LOGE("invalid");
		osmdb_ostream_reset(self);
		return 0;
	}

	osmdb_point_t* pt;
	pt = (osmdb_point_t*)
	     osmdb_ostream_add(self, sizeof(osmdb_point_t), NULL);
	if(pt == NULL)
	{
		return 0;
	}

	double lat = node_coord->lat;
	double lon = node_coord->lon;
	osmdb_ostream_coord2pt(self, lat, lon, pt);

	// increment way pts count
	osmdb_way_t* way;
	way = (osmdb_way_t*)
	      osmdb_ostream_data(self, self->offset_way);
	if(way == NULL)
	{
		return 0;
	}
	++way->count;

	return 1;
}

void osmdb_ostream_endWay(osmdb_ostream_t* self)
{
	ASSERT(self);

	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) osmdb_ostream_data(self, 0);
	if(tile == NULL)
	{
		return;
	}

	if(self->offset_rel)
	{
		osmdb_rel_t* rel;
		rel = (osmdb_rel_t*)
		      osmdb_ostream_data(self, self->offset_rel);
		++rel->count;
	}
	else
	{
		++tile->count_ways;
	}

	self->offset_way = 0;
}

int osmdb_ostream_addNode(osmdb_ostream_t* self,
                          osmdb_nodeInfo_t* node_info,
                          osmdb_nodeCoord_t* node_coord)
{
	ASSERT(self);
	ASSERT(node_info);
	ASSERT(node_coord);

	// add node
	osmdb_node_t* node;
	size_t        offset_node;
	node = (osmdb_node_t*)
	       osmdb_ostream_add(self, sizeof(osmdb_node_t),
	                         &offset_node);
	if(node == NULL)
	{
		return 0;
	}
	node->class = node_info->class;
	node->ele   = node_info->ele;

	double lat = node_coord->lat;
	double lon = node_coord->lon;
	osmdb_ostream_coord2pt(self, lat, lon, &node->pt);

	// initialize name
	char* name      = osmdb_nodeInfo_name(node_info);
	int   size_name = node_info->size_name;
	if(name)
	{
		void* ptr = osmdb_ostream_add(self, size_name, NULL);
		if(ptr == NULL)
		{
			return 0;
		}

		// restore node in case data was reallocated
		node = (osmdb_node_t*)
		       osmdb_ostream_data(self, offset_node);

		memcpy(ptr, (const void*) name, (size_t) size_name);
	}
	node->size_name = size_name;

	osmdb_tile_t* tile;
	tile = (osmdb_tile_t*) osmdb_ostream_data(self, 0);
	if(tile == NULL)
	{
		return 0;
	}
	++tile->count_nodes;

	return 1;
}

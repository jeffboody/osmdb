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

#define LOG_TAG "osmdb"
#include "../../libcc/math/cc_pow2n.h"
#include "../../libcc/cc_log.h"
#include "../../libcc/cc_memory.h"
#include "osmdb_tile.h"

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_tile_validateName(size_t* _offset, int size_name,
	                    size_t* _size, void* data)
{
	ASSERT(_offset);
	ASSERT(_size);
	ASSERT(data);

	if(size_name == 0)
	{
		// ignore
		return 1;
	}
	else if(size_name < 0)
	{
		LOGE("invalid size_name=%i", size_name);
		return 0;
	}

	size_t offset = *_offset;
	size_t size   = *_size;
	size_t dsize  = (size_t) size_name;
	if(size < dsize)
	{
		LOGE("invalid size=%" PRIu64 ", size_name=%i",
		     (uint64_t) size, size_name);
		return 0;
	}

	// name must be terminated by null character
	char* ptr  = (char*) data;
	char* name = (char*) &ptr[offset];
	if(name[size_name - 1] != '\0')
	{
		LOGE("invalid name");
		return 0;
	}

	*_offset = offset + dsize;
	*_size   = size - dsize;

	return 1;
}

static int
osmdb_tile_validateNode(size_t* _offset,
                        size_t* _size, void* data,
                        osmdb_tileParser_t* parser)
{
	// parser may be NULL
	ASSERT(_offset);
	ASSERT(_size);
	ASSERT(data);

	size_t offset = *_offset;
	size_t size   = *_size;
	size_t dsize  = sizeof(osmdb_node_t);
	if(size < dsize)
	{
		LOGE("invalid size=%" PRIu64, (uint64_t) size);
		return 0;
	}

	*_offset = offset + dsize;
	*_size   = size - dsize;

	char*         ptr  = (char*) data;
	osmdb_node_t* node = (osmdb_node_t*) &ptr[offset];
	if(osmdb_tile_validateName(_offset, node->size_name,
	                           _size, data) == 0)
	{
		return 0;
	}

	if(parser)
	{
		osmdb_tileParser_nodeFn node_fn = parser->node_fn;
		return (*node_fn)(parser->priv, node);
	}

	return 1;
}

static int
osmdb_tile_validateWayPts(size_t* _offset, int count,
                          size_t* _size, void* data)
{
	ASSERT(_offset);
	ASSERT(_size);
	ASSERT(data);

	if(count < 0)
	{
		LOGE("invalid count=%i", count);
		return 0;
	}
	else if(count == 0)
	{
		return 1;
	}

	size_t offset = *_offset;
	size_t size   = *_size;
	size_t dsize  = count*sizeof(osmdb_point_t);
	if(size < dsize)
	{
		LOGE("invalid count=%i, size=%" PRIu64,
		     count, (uint64_t) size);
		return 0;
	}

	*_offset = offset + dsize;
	*_size   = size - dsize;

	return 1;
}

static int
osmdb_tile_validateWay(size_t* _offset,
                       size_t* _size, void* data,
                       osmdb_tileParser_t* parser,
                       int member)
{
	// parser may be NULL
	ASSERT(_offset);
	ASSERT(_size);
	ASSERT(data);

	size_t offset = *_offset;
	size_t size   = *_size;
	size_t dsize  = sizeof(osmdb_way_t);
	if(size < dsize)
	{
		LOGE("invalid size=%" PRIu64, (uint64_t) size);
		return 0;
	}

	*_offset = offset + dsize;
	*_size   = size - dsize;

	char* ptr = (char*) data;
	osmdb_way_t* way = (osmdb_way_t*) &ptr[offset];
	if(osmdb_tile_validateName(_offset, way->size_name,
	                           _size, data) == 0)
	{
		return 0;
	}

	if(osmdb_tile_validateWayPts(_offset, way->count,
	                             _size, data) == 0)
	{
		return 0;
	}

	if(parser)
	{
		osmdb_tileParser_wayFn way_fn = parser->way_fn;
		if(member)
		{
			way_fn = parser->member_fn;
		}
		return (*way_fn)(parser->priv, way);
	}

	return 1;
}

static int
osmdb_tile_validateRel(size_t* _offset,
                       size_t* _size, void* data,
                       osmdb_tileParser_t* parser)
{
	// parser may be NULL
	ASSERT(_offset);
	ASSERT(_size);
	ASSERT(data);

	size_t offset = *_offset;
	size_t size   = *_size;
	size_t dsize  = sizeof(osmdb_rel_t);
	if(size < dsize)
	{
		LOGE("invalid size=%" PRIu64, (uint64_t) size);
		return 0;
	}

	*_offset = offset + dsize;
	*_size   = size - dsize;

	char* ptr = (char*) data;
	osmdb_rel_t* rel = (osmdb_rel_t*) &ptr[offset];
	if(osmdb_tile_validateName(_offset, rel->size_name,
	                           _size, data) == 0)
	{
		return 0;
	}

	if(parser)
	{
		osmdb_tileParser_relFn rel_fn = parser->rel_fn;
		if((*rel_fn)(parser->priv, rel) == 0)
		{
			return 0;
		}
	}

	int i;
	for(i = 0; i < rel->count; ++i)
	{
		if(osmdb_tile_validateWay(_offset, _size, data,
		                          parser, 1) == 0)
		{
			return 0;
		}
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

char* osmdb_node_name(osmdb_node_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_node_t));
}

char* osmdb_way_name(osmdb_way_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_way_t));
}

osmdb_point_t* osmdb_way_pts(osmdb_way_t* self)
{
	ASSERT(self);

	if(self->count == 0)
	{
		return NULL;
	}

	return (osmdb_point_t*)
	       (((void*) self) + sizeof(osmdb_way_t) +
	        self->size_name);
}

char* osmdb_rel_name(osmdb_rel_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_rel_t));
}

osmdb_tile_t* osmdb_tile_new(size_t size, void* data,
                             osmdb_tileParser_t* parser)
{
	// parser may be NULL
	ASSERT(data);

	osmdb_tile_t* self  = (osmdb_tile_t*) data;
	size_t        dsize = sizeof(osmdb_tile_t);
	if((self == NULL) || (size < dsize))
	{
		LOGE("invalid tile=%p, size=%i", self, (int) size);
		return NULL;
	}
	size -= dsize;

	// check header
	if((self->magic   != OSMDB_TILE_MAGIC) &&
	   (self->version != OSMDB_TILE_VERSION))
	{
		LOGE("invalid magic=0x%X:%0x%X, version=%i:%i",
		     self->magic, OSMDB_TILE_MAGIC,
		     self->version, OSMDB_TILE_VERSION);
		return NULL;
	}

	// check address
	if((self->zoom < 0) || (self->zoom > 15) ||
	   (self->x < 0) || (self->x >= cc_pow2n(self->zoom)) ||
	   (self->y < 0) || (self->y >= cc_pow2n(self->zoom)))
	{
		LOGE("invalid %i/%i/%i",
		     self->zoom, self->x, self->y);
		return NULL;
	}

	// check count
	if((self->count_rels  < 0) ||
	   (self->count_ways  < 0) ||
	   (self->count_nodes < 0))
	{
		LOGE("invalid %i/%i/%i",
		     self->count_rels, self->count_ways,
		     self->count_nodes);
		return NULL;
	}

	int i;
	size_t offset = dsize;
	for(i = 0; i < self->count_rels; ++i)
	{
		if(osmdb_tile_validateRel(&offset, &size, data,
		                          parser) == 0)
		{
			return NULL;
		}
	}

	for(i = 0; i < self->count_ways; ++i)
	{
		if(osmdb_tile_validateWay(&offset, &size, data,
		                          parser, 0) == 0)
		{
			return NULL;
		}
	}

	for(i = 0; i < self->count_nodes; ++i)
	{
		if(osmdb_tile_validateNode(&offset, &size, data,
		                           parser) == 0)
		{
			return NULL;
		}
	}

	if(size != 0)
	{
		LOGE("invalid size=%" PRIu64, (uint64_t) size);
		return NULL;
	}

	return self;
}

osmdb_tile_t*
osmdb_tile_newNull(int64_t changeset,
                   int zoom, int x, int y)
{
	osmdb_tile_t* self;
	self = (osmdb_tile_t*) CALLOC(1, sizeof(osmdb_tile_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->magic     = OSMDB_TILE_MAGIC;
	self->version   = OSMDB_TILE_VERSION;
	self->changeset = changeset;
	self->zoom      = zoom;
	self->x         = x;
	self->y         = y;

	return self;
}

void osmdb_tile_delete(osmdb_tile_t** _self)
{
	ASSERT(_self);

	osmdb_tile_t* self = *_self;
	if(self)
	{
		FREE(self);
		*_self = NULL;
	}
}

void osmdb_tile_range(osmdb_tile_t* self,
                      osmdb_range_t* range)
{
	ASSERT(self);
	ASSERT(range);

	osmdb_range_init(range, self->zoom, self->x, self->y);
}

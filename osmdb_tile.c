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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "libxmlstream/xml_ostream.h"
#include "osmdb_tile.h"
#include "osmdb_util.h"
#include "osmdb_index.h"
#include "osmdb_parser.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

const int OSMDB_TILE_ONE = 1;

/***********************************************************
* private                                                  *
***********************************************************/

static int osmdb_tile_finish(osmdb_tile_t* self)
{
	assert(self);

	int success = 1;
	xml_ostream_t* os = NULL;
	if(self->dirty)
	{
		char fname[256];
		osmdb_tile_fname(self->base,
		                 self->zoom, self->x, self->y,
		                 fname);

		// ignore error
		osmdb_mkdir(fname);

		os = xml_ostream_newGz(fname);
		if(os == NULL)
		{
			success = 0;
		}
		else
		{
			success &= xml_ostream_begin(os, "osmdb");
		}
	}

	a3d_hashmapIter_t  iterator;
	a3d_hashmapIter_t* iter;
	iter = a3d_hashmap_head(self->hash_nodes, &iterator);
	while(iter)
	{
		if(os)
		{
			success &= xml_ostream_begin(os, "n");
			success &= xml_ostream_attr(os, "ref",
			                            a3d_hashmap_key(iter));
			success &= xml_ostream_end(os);
		}
		a3d_hashmap_remove(self->hash_nodes, &iter);
	}

	iter = a3d_hashmap_head(self->hash_ways, &iterator);
	while(iter)
	{
		if(os)
		{
			success &= xml_ostream_begin(os, "w");
			success &= xml_ostream_attr(os, "ref",
			                            a3d_hashmap_key(iter));
			success &= xml_ostream_end(os);
		}
		a3d_hashmap_remove(self->hash_ways, &iter);
	}

	iter = a3d_hashmap_head(self->hash_relations, &iterator);
	while(iter)
	{
		if(os)
		{
			success &= xml_ostream_begin(os, "r");
			success &= xml_ostream_attr(os, "ref",
			                            a3d_hashmap_key(iter));
			success &= xml_ostream_end(os);
		}
		a3d_hashmap_remove(self->hash_relations, &iter);
	}

	if(os)
	{
		success &= xml_ostream_end(os);
		success &= xml_ostream_complete(os);
		xml_ostream_delete(&os);
	}

	self->size  = 0;
	self->dirty = 0;

	return success;
}

static int nodeFn(void* priv, osmdb_node_t* node)
{
	assert(priv);
	assert(node);

	// nodes not allowed in tiles
	return 1;
}

static int wayFn(void* priv, osmdb_way_t* way)
{
	assert(priv);
	assert(way);

	// ways not allowed in tiles
	return 0;
}

static int relationFn(void* priv,
                      osmdb_relation_t* relation)
{
	assert(priv);
	assert(relation);

	// relations not allowed in tiles
	return 0;
}

static int nodeRefFn(void* priv, double ref)
{
	assert(priv);

	osmdb_tile_t*  self = (osmdb_tile_t*) priv;
	a3d_hashmap_t* hash = self->hash_nodes;

	a3d_hashmapIter_t iter;
	if(a3d_hashmap_addf(hash, &iter,
	                    (const void*) &OSMDB_TILE_ONE,
	                    "%0.0lf", ref) == 0)
	{
		return 0;
	}
	++self->size;

	return 1;
}

static int wayRefFn(void* priv, double ref)
{
	assert(priv);

	osmdb_tile_t*  self = (osmdb_tile_t*) priv;
	a3d_hashmap_t* hash = self->hash_ways;

	a3d_hashmapIter_t iter;
	if(a3d_hashmap_addf(hash, &iter,
	                    (const void*) &OSMDB_TILE_ONE,
	                    "%0.0lf", ref) == 0)
	{
		return 0;
	}
	++self->size;

	return 1;
}

static int relationRefFn(void* priv, double ref)
{
	assert(priv);

	osmdb_tile_t*  self = (osmdb_tile_t*) priv;
	a3d_hashmap_t* hash = self->hash_relations;

	a3d_hashmapIter_t iter;
	if(a3d_hashmap_addf(hash, &iter,
	                    (const void*) &OSMDB_TILE_ONE,
	                    "%0.0lf", ref) == 0)
	{
		return 0;
	}
	++self->size;

	return 1;
}

static int osmdb_tile_import(osmdb_tile_t* self)
{
	assert(self);

	char fname[256];
	osmdb_tile_fname(self->base,
	                 self->zoom, self->x, self->y,
	                 fname);

	if(osmdb_parse(fname, (void*) self,
	               nodeFn, wayFn, relationFn,
	               nodeRefFn, wayRefFn, relationRefFn) == 0)
	{
		osmdb_tile_finish(self);
		return 0;
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_tile_t* osmdb_tile_new(int zoom, int x, int y,
                             const char* base,
                             int import, int* dsize)
{
	assert(base);
	assert(dsize);

	osmdb_tile_t* self = (osmdb_tile_t*)
	                     malloc(sizeof(osmdb_tile_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->hash_nodes = a3d_hashmap_new();
	if(self->hash_nodes == NULL)
	{
		goto fail_hash_nodes;
	}

	self->hash_ways = a3d_hashmap_new();
	if(self->hash_ways == NULL)
	{
		goto fail_hash_ways;
	}

	self->hash_relations = a3d_hashmap_new();
	if(self->hash_relations == NULL)
	{
		goto fail_hash_relations;
	}

	self->base  = base;
	self->zoom  = zoom;
	self->x     = x;
	self->y     = y;
	self->size  = 0;
	self->dirty = 0;

	// optionally import tile
	if(import && (osmdb_tile_import(self) == 0))
	{
		goto fail_import;
	}
	*dsize = self->size;

	// success
	return self;

	// failure
	fail_import:
		a3d_hashmap_delete(&self->hash_relations);
	fail_hash_relations:
		a3d_hashmap_delete(&self->hash_ways);
	fail_hash_ways:
		a3d_hashmap_delete(&self->hash_nodes);
	fail_hash_nodes:
		free(self);
	return NULL;
}

int osmdb_tile_delete(osmdb_tile_t** _self, int* dsize)
{
	assert(_self);
	assert(dsize);

	*dsize = 0;

	int success = 1;
	osmdb_tile_t* self = *_self;
	if(self)
	{
		*dsize = self->size;
		success = osmdb_tile_finish(self);
		a3d_hashmap_delete(&self->hash_nodes);
		a3d_hashmap_delete(&self->hash_ways);
		a3d_hashmap_delete(&self->hash_relations);
		free(self);
		*_self = NULL;
	}
	return success;
}

int osmdb_tile_find(osmdb_tile_t* self,
                    int type, double id)
{
	assert(self);

	a3d_hashmap_t* hash;
	if(type == OSMDB_TYPE_NODE)
	{
		hash = self->hash_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		hash = self->hash_ways;
	}
	else
	{
		hash = self->hash_relations;
	}

	a3d_hashmapIter_t iter;
	if(a3d_hashmap_findf(hash, &iter, "%0.0lf", id))
	{
		return 1;
	}

	return 0;
}

int osmdb_tile_add(osmdb_tile_t* self,
                   int type, double id)
{
	assert(self);

	a3d_hashmap_t* hash;
	if(type == OSMDB_TYPE_NODE)
	{
		hash = self->hash_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		hash = self->hash_ways;
	}
	else
	{
		hash = self->hash_relations;
	}

	a3d_hashmapIter_t iter;
	if(a3d_hashmap_addf(hash, &iter,
	                    (const void*) &OSMDB_TILE_ONE,
	                    "%0.0lf", id) == 0)
	{
		return 0;
	}

	++self->size;
	self->dirty = 1;
	return 1;
}

void osmdb_tile_fname(const char* base,
                      int zoom, int x, int y,
                      char* fname)
{
	assert(base);
	assert(fname);

	snprintf(fname, 256, "%s/tile/%i/%i/%i.xmlz",
	         base, zoom, x, y);
}

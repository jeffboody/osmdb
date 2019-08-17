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
#include "../libxmlstream/xml_ostream.h"
#include "osmdb_tile.h"
#include "osmdb_util.h"
#include "osmdb_index.h"
#include "osmdb_parser.h"

#define LOG_TAG "osmdb"
#include "../libxmlstream/xml_log.h"

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

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(self->map_nodes, &iterator);
	while(iter)
	{
		if(os)
		{
			success &= xml_ostream_begin(os, "n");
			success &= xml_ostream_attr(os, "ref",
			                            cc_map_key(iter));
			success &= xml_ostream_end(os);
		}
		cc_map_remove(self->map_nodes, &iter);
	}

	iter = cc_map_head(self->map_ways, &iterator);
	while(iter)
	{
		if(os)
		{
			success &= xml_ostream_begin(os, "w");
			success &= xml_ostream_attr(os, "ref",
			                            cc_map_key(iter));
			success &= xml_ostream_end(os);
		}
		cc_map_remove(self->map_ways, &iter);
	}

	iter = cc_map_head(self->map_relations, &iterator);
	while(iter)
	{
		if(os)
		{
			success &= xml_ostream_begin(os, "r");
			success &= xml_ostream_attr(os, "ref",
			                            cc_map_key(iter));
			success &= xml_ostream_end(os);
		}
		cc_map_remove(self->map_relations, &iter);
	}

	if(os)
	{
		success &= xml_ostream_end(os);
		success &= xml_ostream_complete(os);
		xml_ostream_delete(&os);
	}

	self->dirty = 0;

	return success;
}

static int nodeRefFn(void* priv, double ref)
{
	assert(priv);

	osmdb_tile_t*  self = (osmdb_tile_t*) priv;
	cc_map_t* map = self->map_nodes;

	if(cc_map_addf(map, (const void*) &OSMDB_TILE_ONE,
	               "%0.0lf", ref) == 0)
	{
		return 0;
	}

	return 1;
}

static int wayRefFn(void* priv, double ref)
{
	assert(priv);

	osmdb_tile_t*  self = (osmdb_tile_t*) priv;
	cc_map_t* map = self->map_ways;

	if(cc_map_addf(map, (const void*) &OSMDB_TILE_ONE,
	               "%0.0lf", ref) == 0)
	{
		return 0;
	}

	return 1;
}

static int relationRefFn(void* priv, double ref)
{
	assert(priv);

	osmdb_tile_t*  self = (osmdb_tile_t*) priv;
	cc_map_t* map = self->map_relations;

	if(cc_map_addf(map, (const void*) &OSMDB_TILE_ONE,
	               "%0.0lf", ref) == 0)
	{
		return 0;
	}

	return 1;
}

static int osmdb_tile_import(osmdb_tile_t* self)
{
	assert(self);

	char fname[256];
	osmdb_tile_fname(self->base,
	                 self->zoom, self->x, self->y,
	                 fname);

	if(osmdb_parseRefs(fname, (void*) self,
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
                             const char* base, int import)
{
	assert(base);

	osmdb_tile_t* self = (osmdb_tile_t*)
	                     malloc(sizeof(osmdb_tile_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->map_nodes = cc_map_new();
	if(self->map_nodes == NULL)
	{
		goto fail_map_nodes;
	}

	self->map_ways = cc_map_new();
	if(self->map_ways == NULL)
	{
		goto fail_map_ways;
	}

	self->map_relations = cc_map_new();
	if(self->map_relations == NULL)
	{
		goto fail_map_relations;
	}

	self->base  = base;
	self->zoom  = zoom;
	self->x     = x;
	self->y     = y;
	self->dirty = 0;

	// optionally import tile
	if(import && (osmdb_tile_import(self) == 0))
	{
		goto fail_import;
	}

	// success
	return self;

	// failure
	fail_import:
		cc_map_delete(&self->map_relations);
	fail_map_relations:
		cc_map_delete(&self->map_ways);
	fail_map_ways:
		cc_map_delete(&self->map_nodes);
	fail_map_nodes:
		free(self);
	return NULL;
}

int osmdb_tile_delete(osmdb_tile_t** _self)
{
	assert(_self);

	int success = 1;
	osmdb_tile_t* self = *_self;
	if(self)
	{
		success = osmdb_tile_finish(self);
		cc_map_delete(&self->map_nodes);
		cc_map_delete(&self->map_ways);
		cc_map_delete(&self->map_relations);
		free(self);
		*_self = NULL;
	}
	return success;
}

int osmdb_tile_size(osmdb_tile_t* self)
{
	assert(self);

	return (int)
	       (cc_map_sizeof(self->map_nodes) +
	        cc_map_sizeof(self->map_ways)  +
	        cc_map_sizeof(self->map_relations));
}

int osmdb_tile_find(osmdb_tile_t* self,
                    int type, double id)
{
	assert(self);

	cc_map_t* map;
	if(type == OSMDB_TYPE_NODE)
	{
		map = self->map_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		map = self->map_ways;
	}
	else
	{
		map = self->map_relations;
	}

	cc_mapIter_t iter;
	if(cc_map_findf(map, &iter, "%0.0lf", id))
	{
		return 1;
	}

	return 0;
}

int osmdb_tile_add(osmdb_tile_t* self,
                   int type, double id)
{
	assert(self);

	cc_map_t* map;
	if(type == OSMDB_TYPE_NODE)
	{
		map = self->map_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		map = self->map_ways;
	}
	else
	{
		map = self->map_relations;
	}

	if(cc_map_addf(map, (const void*) &OSMDB_TILE_ONE,
	               "%0.0lf", id) == 0)
	{
		return 0;
	}

	self->dirty = 1;
	return 1;
}

void osmdb_tile_fname(const char* base,
                      int zoom, int x, int y,
                      char* fname)
{
	assert(base);
	assert(fname);

	snprintf(fname, 256, "%s/tile/%i/%i/%i.xml.gz",
	         base, zoom, x, y);
}

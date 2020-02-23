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

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "../libxmlstream/xml_ostream.h"
#include "osmdb_chunk.h"
#include "osmdb_index.h"
#include "osmdb_node.h"
#include "osmdb_parser.h"
#include "osmdb_relation.h"
#include "osmdb_util.h"
#include "osmdb_way.h"

/***********************************************************
* private                                                  *
***********************************************************/

static int osmdb_chunk_finish(osmdb_chunk_t* self)
{
	ASSERT(self);

	int success = 1;
	xml_ostream_t* os = NULL;
	if(self->dirty)
	{
		char fname[256];
		osmdb_chunk_fname(self->base, self->type,
		                  self->idu, fname);

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
	iter = cc_map_head(self->map, &iterator);
	while(iter)
	{
		if((self->type == OSMDB_TYPE_NODE) ||
		   (self->type == OSMDB_TYPE_CTRNODE))
		{
			osmdb_node_t* n;
			n = (osmdb_node_t*) cc_map_remove(self->map, &iter);
			if(os)
			{
				success &= osmdb_node_export(n, os);
			}
			osmdb_node_delete(&n);
		}
		else if(self->type == OSMDB_TYPE_WAY)
		{
			osmdb_way_t* w;
			w = (osmdb_way_t*) cc_map_remove(self->map, &iter);
			if(os)
			{
				success &= osmdb_way_export(w, os);
			}
			osmdb_way_delete(&w);
		}
		else if(self->type == OSMDB_TYPE_RELATION)
		{
			osmdb_relation_t* r;
			r = (osmdb_relation_t*) cc_map_remove(self->map, &iter);
			if(os)
			{
				success &= osmdb_relation_export(r, os);
			}
			osmdb_relation_delete(&r);
		}
		else if((self->type == OSMDB_TYPE_NODEREF) ||
		        (self->type == OSMDB_TYPE_CTRNODEREF))
		{
			double* ref;
			ref = (double*)
			      cc_map_remove(self->map, &iter);
			if(os)
			{
				success &= xml_ostream_begin(os, "n");
				success &= xml_ostream_attrf(os, "ref", "%0.0lf", *ref);
				success &= xml_ostream_end(os);
			}
			FREE(ref);
		}
		else if((self->type == OSMDB_TYPE_WAYREF) ||
		        (self->type == OSMDB_TYPE_CTRWAYREF))
		{
			double* ref;
			ref = (double*) cc_map_remove(self->map, &iter);
			if(os)
			{
				success &= xml_ostream_begin(os, "w");
				success &= xml_ostream_attrf(os, "ref", "%0.0lf", *ref);
				success &= xml_ostream_end(os);
			}
			FREE(ref);
		}
		else
		{
			LOGE("invalid type=%i", self->type);
			success = 0;
		}
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

static int
osmdb_chunk_nodeFn(void* priv,
                   osmdb_node_t* node)
{
	ASSERT(priv);
	ASSERT(node);

	osmdb_chunk_t* self = (osmdb_chunk_t*) priv;
	if((self->type == OSMDB_TYPE_NODE) ||
	   (self->type == OSMDB_TYPE_CTRNODE))
	{
		// accept
	}
	else
	{
		LOGE("invalid id=%0.0lf", node->id);
		return 0;
	}

	double idu;
	double idl;
	osmdb_splitId(node->id, &idu, &idl);

	if(cc_map_addf(self->map, (const void*) node, "%0.0lf",
	               idl) == 0)
	{
		return 0;
	}

	self->size += osmdb_node_size(node);
	return 1;
}

static int
osmdb_chunk_wayFn(void* priv,
                  osmdb_way_t* way)
{
	ASSERT(priv);
	ASSERT(way);

	osmdb_chunk_t* self = (osmdb_chunk_t*) priv;
	if(self->type == OSMDB_TYPE_WAY)
	{
		// accept
	}
	else
	{
		LOGE("invalid id=%0.0lf", way->id);
		return 0;
	}

	double idu;
	double idl;
	osmdb_splitId(way->id, &idu, &idl);

	if(cc_map_addf(self->map, (const void*) way, "%0.0lf",
	               idl) == 0)
	{
		return 0;
	}

	self->size += osmdb_way_size(way);
	return 1;
}

static int
osmdb_chunk_relationFn(void* priv,
                       osmdb_relation_t* relation)
{
	ASSERT(priv);
	ASSERT(relation);

	osmdb_chunk_t* self = (osmdb_chunk_t*) priv;
	if(self->type == OSMDB_TYPE_RELATION)
	{
		// accept
	}
	else
	{
		LOGE("invalid id=%0.0lf", relation->id);
		return 0;
	}

	double idu;
	double idl;
	osmdb_splitId(relation->id, &idu, &idl);

	if(cc_map_addf(self->map, (const void*) relation, "%0.0lf",
	               idl) == 0)
	{
		return 0;
	}

	self->size += osmdb_relation_size(relation);
	return 1;
}

static int
osmdb_chunk_nodeRefFn(void* priv,
                      double ref)
{
	ASSERT(priv);

	osmdb_chunk_t* self = (osmdb_chunk_t*) priv;
	if((self->type == OSMDB_TYPE_NODEREF) ||
	   (self->type == OSMDB_TYPE_CTRNODEREF))
	{
		// accept
	}
	else
	{
		LOGE("invalid ref=%0.0lf", ref);
		return 0;
	}

	double idu;
	double idl;
	osmdb_splitId(ref, &idu, &idl);

	double* _ref = (double*)
	               MALLOC(sizeof(double));
	if(_ref == NULL)
	{
		LOGE("MALLOC failed");
		return 0;
	}
	*_ref = ref;

	if(cc_map_addf(self->map, (const void*) _ref, "%0.0lf",
	               idl) == 0)
	{
		goto fail_add;
	}

	// success
	self->size += (int) sizeof(double);
	return 1;

	// failure
	fail_add:
		FREE(_ref);
	return 0;
}

static int
osmdb_chunk_wayRefFn(void* priv,
                     double ref)
{
	ASSERT(priv);

	osmdb_chunk_t* self = (osmdb_chunk_t*) priv;
	if((self->type == OSMDB_TYPE_WAYREF) ||
	   (self->type == OSMDB_TYPE_CTRWAYREF))
	{
		// accept
	}
	else
	{
		LOGE("invalid ref=%0.0lf", ref);
		return 0;
	}

	double idu;
	double idl;
	osmdb_splitId(ref, &idu, &idl);

	double* _ref = (double*)
	               MALLOC(sizeof(double));
	if(_ref == NULL)
	{
		LOGE("MALLOC failed");
		return 0;
	}
	*_ref = ref;

	if(cc_map_addf(self->map, (const void*) _ref, "%0.0lf",
	               idl) == 0)
	{
		goto fail_add;
	}

	// success
	self->size += (int) sizeof(double);
	return 1;

	// failure
	fail_add:
		FREE(_ref);
	return 0;
}

static int
osmdb_chunk_relationRefFn(void* priv,
                          double ref)
{
	LOGE("invalid ref=%0.0lf", ref);
	return 0;
}

static int osmdb_chunk_import(osmdb_chunk_t* self)
{
	ASSERT(self);

	char fname[256];
	osmdb_chunk_fname(self->base, self->type,
	                  self->idu, fname);

	if((self->type == OSMDB_TYPE_NODE)     ||
	   (self->type == OSMDB_TYPE_WAY)      ||
	   (self->type == OSMDB_TYPE_RELATION) ||
	   (self->type == OSMDB_TYPE_CTRNODE))
	{
		if(osmdb_parse(fname, (void*) self,
		               osmdb_chunk_nodeFn,
		               osmdb_chunk_wayFn,
		               osmdb_chunk_relationFn) == 0)
		{
			osmdb_chunk_finish(self);
			return 0;
		}
	}
	else if((self->type == OSMDB_TYPE_NODEREF)    ||
	        (self->type == OSMDB_TYPE_WAYREF)     ||
	        (self->type == OSMDB_TYPE_CTRNODEREF) ||
	        (self->type == OSMDB_TYPE_CTRWAYREF))
	{
		if(osmdb_parseRefs(fname, (void*) self,
		                   osmdb_chunk_nodeRefFn,
		                   osmdb_chunk_wayRefFn,
		                   osmdb_chunk_relationRefFn) == 0)
		{
			osmdb_chunk_finish(self);
			return 0;
		}
	}
	else
	{
		LOGE("invalid type=%i", self->type);
		return 0;
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_chunk_t* osmdb_chunk_new(const char* base,
                               double idu, int type,
                               int import, int* dsize)
{
	ASSERT(base);
	ASSERT(dsize);

	osmdb_chunk_t* self = (osmdb_chunk_t*)
	                      MALLOC(sizeof(osmdb_chunk_t));
	if(self == NULL)
	{
		LOGE("MALLOC failed");
		return NULL;
	}

	self->map = cc_map_new();
	if(self->map == NULL)
	{
		goto fail_map;
	}

	self->base   = base;
	self->idu    = idu;
	self->type   = type;
	self->size   = 0;
	self->dirty  = 0;
	self->locked = 0;

	// optionally import chunk
	if(import && (osmdb_chunk_import(self) == 0))
	{
		goto fail_import;
	}
	*dsize = self->size;

	// success
	return self;

	// failure
	fail_import:
		cc_map_delete(&self->map);
	fail_map:
		FREE(self);
	return NULL;
}

int osmdb_chunk_delete(osmdb_chunk_t** _self, int* dsize)
{
	ASSERT(_self);
	ASSERT(dsize);

	*dsize = 0;

	int success = 1;
	osmdb_chunk_t* self = *_self;
	if(self)
	{
		*dsize = self->size;
		success = osmdb_chunk_finish(self);
		cc_map_delete(&self->map);
		FREE(self);
		*_self = NULL;
	}
	return success;
}

void osmdb_chunk_lock(osmdb_chunk_t* self)
{
	ASSERT(self);

	self->locked = 1;
}

void osmdb_chunk_unlock(osmdb_chunk_t* self)
{
	ASSERT(self);

	self->locked = 0;
}

int osmdb_chunk_locked(osmdb_chunk_t* self)
{
	ASSERT(self);

	return self->locked;
}

const void* osmdb_chunk_find(osmdb_chunk_t* self, double idl)
{
	ASSERT(self);

	cc_mapIter_t iterator;
	return cc_map_findf(self->map, &iterator, "%0.0lf", idl);
}

int osmdb_chunk_add(osmdb_chunk_t* self,
                    const void* data, double idl, int dsize)
{
	ASSERT(self);
	ASSERT(data);

	if(cc_map_addf(self->map, data, "%0.0f", idl) == 0)
	{
		return 0;
	}

	self->size += dsize;
	self->dirty = 1;
	return 1;
}

int osmdb_chunk_flush(osmdb_chunk_t* self)
{
	ASSERT(self);

	if(self->dirty == 0)
	{
		return 1;
	}

	int success = 1;
	char fname[256];
	osmdb_chunk_fname(self->base, self->type,
	                  self->idu, fname);

	// ignore error
	osmdb_mkdir(fname);

	xml_ostream_t* os = xml_ostream_newGz(fname);
	if(os == NULL)
	{
		return 0;
	}
	success &= xml_ostream_begin(os, "osmdb");

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(self->map, &iterator);
	while(iter)
	{
		if((self->type == OSMDB_TYPE_NODE) ||
		   (self->type == OSMDB_TYPE_CTRNODE))
		{
			osmdb_node_t* n;
			n = (osmdb_node_t*) cc_map_val(iter);
			success &= osmdb_node_export(n, os);
		}
		else if(self->type == OSMDB_TYPE_WAY)
		{
			osmdb_way_t* w;
			w = (osmdb_way_t*) cc_map_val(iter);
			success &= osmdb_way_export(w, os);
		}
		else if(self->type == OSMDB_TYPE_RELATION)
		{
			osmdb_relation_t* r;
			r = (osmdb_relation_t*) cc_map_val(iter);
			success &= osmdb_relation_export(r, os);
		}
		else if((self->type == OSMDB_TYPE_NODEREF) ||
		        (self->type == OSMDB_TYPE_CTRNODEREF))
		{
			double* ref;
			ref = (double*) cc_map_val(iter);
			success &= xml_ostream_begin(os, "n");
			success &= xml_ostream_attrf(os, "ref", "%0.0lf", *ref);
			success &= xml_ostream_end(os);
		}
		else if((self->type == OSMDB_TYPE_WAYREF) ||
		        (self->type == OSMDB_TYPE_CTRWAYREF))
		{
			double* ref;
			ref = (double*) cc_map_val(iter);
			success &= xml_ostream_begin(os, "w");
			success &= xml_ostream_attrf(os, "ref", "%0.0lf", *ref);
			success &= xml_ostream_end(os);
		}
		else
		{
			LOGE("invalid type=%i", self->type);
			success = 0;
			break;
		}

		iter = cc_map_next(iter);
	}

	success &= xml_ostream_end(os);
	success &= xml_ostream_complete(os);
	xml_ostream_delete(&os);

	self->dirty = 0;

	return success;
}

void osmdb_chunk_fname(const char* base,
                       int type, double idu,
                       char* fname)
{
	ASSERT(base);
	ASSERT(fname);

	if(type == OSMDB_TYPE_NODE)
	{
		snprintf(fname, 256, "%s/node/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		snprintf(fname, 256, "%s/way/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_RELATION)
	{
		snprintf(fname, 256, "%s/relation/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_CTRNODE)
	{
		snprintf(fname, 256, "%s/ctrnode/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_NODEREF)
	{
		snprintf(fname, 256, "%s/noderef/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_WAYREF)
	{
		snprintf(fname, 256, "%s/wayref/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_CTRNODEREF)
	{
		snprintf(fname, 256, "%s/ctrnoderef/%0.0lf.xml.gz",
		         base, idu);
	}
	else if(type == OSMDB_TYPE_CTRWAYREF)
	{
		snprintf(fname, 256, "%s/ctrwayref/%0.0lf.xml.gz",
		         base, idu);
	}
	else
	{
		LOGE("invalid type=%i", type);
		snprintf(fname, 256, "%s/invalid/%0.0lf.xml.gz",
		         base, idu);
	}
}

void osmdb_chunk_path(const char* base,
                      int type, char* path)
{
	ASSERT(base);
	ASSERT(path);

	if(type == OSMDB_TYPE_NODE)
	{
		snprintf(path, 256, "%s/node/", base);
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		snprintf(path, 256, "%s/way/", base);
	}
	else if(type == OSMDB_TYPE_RELATION)
	{
		snprintf(path, 256, "%s/relation/", base);
	}
	else if(type == OSMDB_TYPE_CTRNODE)
	{
		snprintf(path, 256, "%s/ctrnode/", base);
	}
	else if(type == OSMDB_TYPE_NODEREF)
	{
		snprintf(path, 256, "%s/noderef/", base);
	}
	else if(type == OSMDB_TYPE_WAYREF)
	{
		snprintf(path, 256, "%s/wayref/", base);
	}
	else if(type == OSMDB_TYPE_CTRNODEREF)
	{
		snprintf(path, 256, "%s/ctrnoderef/", base);
	}
	else if(type == OSMDB_TYPE_CTRWAYREF)
	{
		snprintf(path, 256, "%s/ctrwayref/", base);
	}
	else
	{
		LOGE("invalid type=%i", type);
		snprintf(path, 256, "%s/invalid/", base);
	}
}

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
#include "osmdb_way.h"
#include "osmdb_util.h"

#define LOG_TAG "osmdb"
#include "../libxmlstream/xml_log.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_way_t* osmdb_way_new(const char** atts, int line)
{
	assert(atts);

	const char* id      = NULL;
	const char* name    = NULL;
	const char* abrev   = NULL;
	const char* class   = NULL;
	const char* layer   = NULL;
	const char* oneway  = NULL;
	const char* bridge  = NULL;
	const char* tunnel  = NULL;
	const char* cutting = NULL;
	const char* latT    = NULL;
	const char* lonL    = NULL;
	const char* latB    = NULL;
	const char* lonR    = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "id") == 0)
		{
			id = atts[idx1];
		}
		else if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "abrev") == 0)
		{
			abrev = atts[idx1];
		}
		else if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		else if(strcmp(atts[idx0], "layer") == 0)
		{
			layer = atts[idx1];
		}
		else if(strcmp(atts[idx0], "oneway") == 0)
		{
			oneway = atts[idx1];
		}
		else if(strcmp(atts[idx0], "bridge") == 0)
		{
			bridge = atts[idx1];
		}
		else if(strcmp(atts[idx0], "tunnel") == 0)
		{
			tunnel = atts[idx1];
		}
		else if(strcmp(atts[idx0], "cutting") == 0)
		{
			cutting = atts[idx1];
		}
		else if(strcmp(atts[idx0], "latT") == 0)
		{
			latT = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lonL") == 0)
		{
			lonL = atts[idx1];
		}
		else if(strcmp(atts[idx0], "latB") == 0)
		{
			latB = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lonR") == 0)
		{
			lonR = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(id == NULL)
	{
		LOGE("invalid line=%i", line);
		return NULL;
	}

	// create the way
	osmdb_way_t* self = (osmdb_way_t*)
	                    calloc(1, sizeof(osmdb_way_t));
	if(self == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	self->nds = a3d_list_new();
	if(self->nds == NULL)
	{
		goto fail_nds;
	}

	self->refcount = 0;
	self->id  = strtod(id, NULL);

	if(name)
	{
		int len = strlen(name) + 1;
		self->name = (char*) malloc(len*sizeof(char));
		if(self->name == NULL)
		{
			LOGE("malloc failed");
			goto fail_name;
		}
		snprintf(self->name, len, "%s", name);
	}

	if(abrev)
	{
		int len = strlen(abrev) + 1;
		self->abrev = (char*) malloc(len*sizeof(char));
		if(self->abrev == NULL)
		{
			LOGE("malloc failed");
			goto fail_abrev;
		}
		snprintf(self->abrev, len, "%s", abrev);
	}

	if(class)
	{
		self->class = osmdb_classNameToCode(class);
	}

	if(layer)
	{
		self->layer = (int) strtol(layer, NULL, 0);
	}

	if(oneway)
	{
		self->oneway = (int) strtol(oneway, NULL, 0);
	}

	if(bridge)
	{
		self->bridge = (int) strtol(bridge, NULL, 0);
	}

	if(tunnel)
	{
		self->tunnel = (int) strtol(tunnel, NULL, 0);
	}

	if(cutting)
	{
		self->cutting = (int) strtol(cutting, NULL, 0);
	}

	if(latT)
	{
		self->latT = strtod(latT, NULL);
	}

	if(lonL)
	{
		self->lonL = strtod(lonL, NULL);
	}

	if(latB)
	{
		self->latB = strtod(latB, NULL);
	}

	if(lonR)
	{
		self->lonR = strtod(lonR, NULL);
	}

	// success
	return self;

	// failure
	fail_abrev:
		free(self->name);
	fail_name:
		a3d_list_delete(&self->nds);
	fail_nds:
		free(self);
	return NULL;
}

osmdb_way_t*
osmdb_way_copyEmpty(osmdb_way_t* self)
{
	assert(self);

	osmdb_way_t* copy = (osmdb_way_t*)
	                    calloc(1, sizeof(osmdb_way_t));
	if(copy == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	copy->nds = a3d_list_new();
	if(copy->nds == NULL)
	{
		goto fail_nds;
	}

	if(self->name)
	{
		int len = strlen(self->name) + 1;
		copy->name = (char*) malloc(len*sizeof(char));
		if(copy->name == NULL)
		{
			LOGE("malloc failed");
			goto fail_name;
		}
		snprintf(copy->name, len, "%s", self->name);
	}

	if(self->abrev)
	{
		int len = strlen(self->abrev) + 1;
		copy->abrev = (char*) malloc(len*sizeof(char));
		if(copy->abrev == NULL)
		{
			LOGE("malloc failed");
			goto fail_abrev;
		}
		snprintf(copy->abrev, len, "%s", self->abrev);
	}

	copy->id      = self->id;
	copy->class   = self->class;
	copy->layer   = self->layer;
	copy->oneway  = self->oneway;
	copy->bridge  = self->bridge;
	copy->tunnel  = self->tunnel;
	copy->cutting = self->cutting;
	copy->latT    = 0.0;
	copy->lonL    = 0.0;
	copy->latB    = 0.0;
	copy->lonR    = 0.0;

	// success
	return copy;

	// failure
	fail_abrev:
		free(copy->name);
	fail_name:
		a3d_list_delete(&copy->nds);
	fail_nds:
		free(copy);
	return NULL;
}

void osmdb_way_delete(osmdb_way_t** _self)
{
	assert(_self);

	osmdb_way_t* self = *_self;
	if(self)
	{
		a3d_listitem_t* iter = a3d_list_head(self->nds);
		while(iter)
		{
			double* ref = (double*)
			              a3d_list_remove(self->nds, &iter);
			free(ref);
		}

		a3d_list_delete(&self->nds);
		free(self->name);
		free(self->abrev);
		free(self);
		*_self = NULL;
	}
}

void osmdb_way_incref(osmdb_way_t* self)
{
	assert(self);

	++self->refcount;
}

int osmdb_way_decref(osmdb_way_t* self)
{
	assert(self);

	--self->refcount;
	return (self->refcount == 0) ? 1 : 0;
}

int osmdb_way_export(osmdb_way_t* self, xml_ostream_t* os)
{
	assert(self);
	assert(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "way");
	ret &= xml_ostream_attrf(os, "id", "%0.0lf", self->id);
	if(self->name)
	{
		ret &= xml_ostream_attr(os, "name", self->name);
	}
	if(self->abrev)
	{
		ret &= xml_ostream_attr(os, "abrev", self->abrev);
	}
	if(self->class)
	{
		ret &= xml_ostream_attr(os, "class",
		                        osmdb_classCodeToName(self->class));
	}
	if(self->layer)
	{
		ret &= xml_ostream_attrf(os, "layer", "%i", self->layer);
	}
	if(self->oneway)
	{
		ret &= xml_ostream_attrf(os, "oneway", "%i", self->oneway);
	}
	if(self->bridge)
	{
		ret &= xml_ostream_attrf(os, "bridge", "%i", self->bridge);
	}
	if(self->tunnel)
	{
		ret &= xml_ostream_attrf(os, "tunnel", "%i", self->tunnel);
	}
	if(self->cutting)
	{
		ret &= xml_ostream_attrf(os, "cutting", "%i", self->cutting);
	}
	if((self->latT == 0.0) && (self->lonL == 0.0) &&
	   (self->latB == 0.0) && (self->lonR == 0.0))
	{
		// skip range
	}
	else
	{
		ret &= xml_ostream_attrf(os, "latT", "%lf", self->latT);
		ret &= xml_ostream_attrf(os, "lonL", "%lf", self->lonL);
		ret &= xml_ostream_attrf(os, "latB", "%lf", self->latB);
		ret &= xml_ostream_attrf(os, "lonR", "%lf", self->lonR);
	}

	a3d_listitem_t* iter = a3d_list_head(self->nds);
	while(iter)
	{
		double* ref = (double*) a3d_list_peekitem(iter);
		ret &= xml_ostream_begin(os, "nd");
		ret &= xml_ostream_attrf(os, "ref", "%0.0lf", *ref);
		ret &= xml_ostream_end(os);
		iter = a3d_list_next(iter);
	}

	ret &= xml_ostream_end(os);

	return ret;
}

int osmdb_way_size(osmdb_way_t* self)
{
	assert(self);

	int size = sizeof(osmdb_way_t);
	if(self->name)
	{
		size += strlen(self->name);
	}
	if(self->abrev)
	{
		size += strlen(self->abrev);
	}
	size += sizeof(double)*a3d_list_size(self->nds);

	return size;
}

int osmdb_way_nd(osmdb_way_t* self, const char** atts, int line)
{
	assert(self);
	assert(atts);

	const char* ref = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "ref") == 0)
		{
			ref = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(ref == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	// add the ref
	if(osmdb_way_ref(self, strtod(ref, NULL)) == 0)
	{
		return 0;
	}

	return 1;
}

void osmdb_way_updateRange(osmdb_way_t* self,
                           osmdb_range_t* range)
{
	assert(self);
	assert(range);

	self->latT = range->latT;
	self->lonL = range->lonL;
	self->latB = range->latB;
	self->lonR = range->lonR;
}

int osmdb_way_ref(osmdb_way_t* self,
                  double ref)
{
	assert(self);

	// create the nd
	double* _ref = (double*) malloc(sizeof(double));
	if(_ref == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	*_ref = ref;

	// add nd to list
	if(a3d_list_append(self->nds, NULL,
	                   (const void*) _ref) == NULL)
	{
		goto fail_append;
	}

	// success
	return 1;

	// failure
	fail_append:
		free(_ref);
	return 0;
}

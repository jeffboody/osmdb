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
#include <string.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "osmdb_node.h"
#include "osmdb_util.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_node_t* osmdb_node_new(const char** atts, int line)
{
	ASSERT(atts);

	const char* id    = NULL;
	const char* lat   = NULL;
	const char* lon   = NULL;
	const char* name  = NULL;
	const char* abrev = NULL;
	const char* ele   = NULL;
	const char* st    = NULL;
	const char* class = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "id") == 0)
		{
			id = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lat") == 0)
		{
			lat = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lon") == 0)
		{
			lon = atts[idx1];
		}
		else if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "abrev") == 0)
		{
			abrev = atts[idx1];
		}
		else if(strcmp(atts[idx0], "ele") == 0)
		{
			ele = atts[idx1];
		}
		else if(strcmp(atts[idx0], "st") == 0)
		{
			st = atts[idx1];
		}
		else if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((id == NULL) || (lat == NULL) || (lon == NULL))
	{
		LOGE("invalid line=%i", line);
		return NULL;
	}

	// create the node
	osmdb_node_t* self = (osmdb_node_t*)
	                     CALLOC(1, sizeof(osmdb_node_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->refcount = 0;
	self->id  = strtod(id, NULL);
	self->lat = strtod(lat, NULL);
	self->lon = strtod(lon, NULL);

	if(name)
	{
		int len = strlen(name) + 1;
		self->name = (char*) MALLOC(len*sizeof(char));
		if(self->name == NULL)
		{
			goto fail_name;
		}
		snprintf(self->name, len, "%s", name);
	}

	if(abrev)
	{
		int len = strlen(abrev) + 1;
		self->abrev = (char*) MALLOC(len*sizeof(char));
		if(self->abrev == NULL)
		{
			goto fail_abrev;
		}
		snprintf(self->abrev, len, "%s", abrev);
	}

	if(ele)
	{
		self->ele = (int) strtol(ele, NULL, 0);
	}

	if(st)
	{
		self->st = osmdb_stNameToCode(st);
	}

	if(class)
	{
		self->class = osmdb_classNameToCode(class);
	}

	// success
	return self;

	// failure
	fail_abrev:
		FREE(self->name);
	fail_name:
		FREE(self);
	return NULL;
}

void osmdb_node_delete(osmdb_node_t** _self)
{
	ASSERT(_self);

	osmdb_node_t* self = *_self;
	if(self)
	{
		FREE(self->name);
		FREE(self->abrev);
		FREE(self);
		*_self = NULL;
	}
}

void osmdb_node_incref(osmdb_node_t* self)
{
	ASSERT(self);

	++self->refcount;
}

int osmdb_node_decref(osmdb_node_t* self)
{
	ASSERT(self);

	--self->refcount;
	return (self->refcount == 0) ? 1 : 0;
}

int osmdb_node_export(osmdb_node_t* self, xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "node");
	ret &= xml_ostream_attrf(os, "id", "%0.0lf", self->id);
	ret &= xml_ostream_attrf(os, "lat", "%lf", self->lat);
	ret &= xml_ostream_attrf(os, "lon", "%lf", self->lon);
	if(self->name)
	{
		ret &= xml_ostream_attr(os, "name", self->name);
	}
	if(self->abrev)
	{
		ret &= xml_ostream_attr(os, "abrev", self->abrev);
	}
	if(self->ele)
	{
		ret &= xml_ostream_attrf(os, "ele", "%i", self->ele);
	}
	if(self->st)
	{
		ret &= xml_ostream_attr(os, "st",
		                        osmdb_stCodeToAbrev(self->st));
	}
	if(self->class)
	{
		ret &= xml_ostream_attr(os, "class",
		                        osmdb_classCodeToName(self->class));
	}
	ret &= xml_ostream_end(os);

	return ret;
}

int osmdb_node_size(osmdb_node_t* self)
{
	ASSERT(self);

	int size = sizeof(osmdb_node_t);
	if(self->name)
	{
		size += strlen(self->name);
	}
	if(self->abrev)
	{
		size += strlen(self->abrev);
	}

	return size;
}

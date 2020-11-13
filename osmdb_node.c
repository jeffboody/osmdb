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

osmdb_node_t* osmdb_node_new(double id,
                             double lat,
                             double lon,
                             const char* name,
                             const char* abrev,
                             int ele,
                             int st,
                             int class)
{
	// name and abrev may be NULL

	// create the node
	osmdb_node_t* self = (osmdb_node_t*)
	                     CALLOC(1, sizeof(osmdb_node_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->base.type = OSMDB_OBJECT_TYPE_NODE;
	self->base.id   = id;
	self->lat       = lat;
	self->lon       = lon;
	self->ele       = ele;
	self->st        = st;
	self->class     = class;

	if(name && (name[0] != '\0'))
	{
		int len = strlen(name) + 1;
		self->name = (char*) MALLOC(len*sizeof(char));
		if(self->name == NULL)
		{
			goto fail_name;
		}
		snprintf(self->name, len, "%s", name);
	}

	if(abrev && (abrev[0] != '\0'))
	{
		int len = strlen(abrev) + 1;
		self->abrev = (char*) MALLOC(len*sizeof(char));
		if(self->abrev == NULL)
		{
			goto fail_abrev;
		}
		snprintf(self->abrev, len, "%s", abrev);
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

osmdb_node_t* osmdb_node_newXml(const char** atts, int line)
{
	ASSERT(atts);

	const char* att_id    = NULL;
	const char* att_lat   = NULL;
	const char* att_lon   = NULL;
	const char* att_name  = NULL;
	const char* att_abrev = NULL;
	const char* att_ele   = NULL;
	const char* att_st    = NULL;
	const char* att_class = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "id") == 0)
		{
			att_id = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lat") == 0)
		{
			att_lat = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lon") == 0)
		{
			att_lon = atts[idx1];
		}
		else if(strcmp(atts[idx0], "name") == 0)
		{
			att_name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "abrev") == 0)
		{
			att_abrev = atts[idx1];
		}
		else if(strcmp(atts[idx0], "ele") == 0)
		{
			att_ele = atts[idx1];
		}
		else if(strcmp(atts[idx0], "st") == 0)
		{
			att_st = atts[idx1];
		}
		else if(strcmp(atts[idx0], "class") == 0)
		{
			att_class = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((att_id == NULL) || (att_lat == NULL) ||
	   (att_lon == NULL))
	{
		LOGE("invalid line=%i", line);
		return NULL;
	}

	double id  = strtod(att_id, NULL);
	double lat = strtod(att_lat, NULL);
	double lon = strtod(att_lon, NULL);

	int ele = 0;
	if(att_ele)
	{
		ele = (int) strtol(att_ele, NULL, 0);
	}

	int st = 0;
	if(att_st)
	{
		st = osmdb_stNameToCode(att_st);
	}

	int class = 0;
	if(att_class)
	{
		class = osmdb_classNameToCode(att_class);
	}

	return osmdb_node_new(id, lat, lon,
	                      att_name, att_abrev,
	                      ele, st, class);
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

	++self->base.refcount;
}

int osmdb_node_decref(osmdb_node_t* self)
{
	ASSERT(self);

	--self->base.refcount;
	return (self->base.refcount == 0) ? 1 : 0;
}

int osmdb_node_export(osmdb_node_t* self, xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "node");
	ret &= xml_ostream_attrf(os, "id", "%0.0lf", self->base.id);
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

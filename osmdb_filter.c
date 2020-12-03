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
#include "../libxmlstream/xml_istream.h"
#include "osmdb_filter.h"
#include "osmdb_util.h"

#define OSMDB_CLASS_SELECTED 1

/***********************************************************
* private                                                  *
***********************************************************/

static osmdb_filterInfo_t*
osmdb_filterInfo_new(int zoom, const char* flags)
{
	// flags may be NULL

	osmdb_filterInfo_t* self;
	self = (osmdb_filterInfo_t*)
	       MALLOC(sizeof(osmdb_filterInfo_t));
	if(self == NULL)
	{
		LOGE("MALLOC failed");
		return NULL;
	}

	// parse flags
	int named  = 0;
	int center = 0;
	if(flags)
	{
		char str[256];
		int  src = 0;
		int  dst = 0;
		while(dst < 256)
		{
			str[dst] = flags[src];
			if((str[dst] == ' ') || (str[dst] == '\t'))
			{
				// discard whitespace
				++src;
				continue;
			}
			else if((str[dst] == ',') || (str[dst] == '\0'))
			{
				// parse the flag
				str[dst] = '\0';
				if(strcmp(str, "center") == 0)
				{
					center = 1;
				}
				else if(strcmp(str, "named") == 0)
				{
					named = 1;
				}
				else
				{
					LOGW("unknown flag=%s", src);
				}

				// end of string
				if(flags[src] == '\0')
				{
					break;
				}

				// next character
				dst = 0;
				++src;
				continue;
			}

			// next character
			++dst;
			++src;
		}
	}

	self->zoom   = zoom;
	self->center = center;
	self->named  = named;

	return self;
}

static void
osmdb_filterInfo_delete(osmdb_filterInfo_t** _self)
{
	ASSERT(_self);

	osmdb_filterInfo_t* self = *_self;
	if(self)
	{
		FREE(self);
		*_self = NULL;
	}
}

static int osmdb_filter_start(void* priv,
                              int line,
                              float progress,
                              const char* name,
                              const char** atts)
{
	ASSERT(priv);
	ASSERT(name);
	ASSERT(atts);

	osmdb_filter_t* self = (osmdb_filter_t*) priv;

	if(strcmp(name, "select") != 0)
	{
		return 1;
	}

	int         zoom  = -1;
	const char* class = NULL;
	const char* flags = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		else if(strcmp(atts[idx0], "zoom") == 0)
		{
			zoom = (int) strtol(atts[idx1], NULL, 0);
		}
		else if(strcmp(atts[idx0], "flags") == 0)
		{
			flags = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((class == NULL) || (zoom < 0))
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	// check for duplicates
	cc_mapIter_t iterator;
	if(cc_map_find(self->info, &iterator, class))
	{
		LOGE("duplicate line=%i", line);
		return 0;
	}

	// create filter info
	osmdb_filterInfo_t* info;
	info = osmdb_filterInfo_new(zoom, flags);
	if(info == NULL)
	{
		return 0;
	}

	// add the filter info
	if(cc_map_add(self->info, (const void*) info, class) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_filterInfo_delete(&info);
	return 0;
}

static int osmdb_filter_end(void* priv,
                            int line,
                            float progress,
                            const char* name,
                            const char* content)
{
	// content may be NULL
	ASSERT(priv);
	ASSERT(name);

	return 1;
}

static void osmdb_filter_discard(osmdb_filter_t* self)
{
	ASSERT(self);

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(self->info, &iterator);
	while(iter)
	{
		osmdb_filterInfo_t* info;
		info = (osmdb_filterInfo_t*)
		       cc_map_remove(self->info, &iter);
		osmdb_filterInfo_delete(&info);
	}
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_filter_t* osmdb_filter_new(const char* fname)
{
	ASSERT(fname);

	osmdb_filter_t* self;
	self = (osmdb_filter_t*) MALLOC(sizeof(osmdb_filter_t));
	if(self == NULL)
	{
		LOGE("MALLOC failed");
		return NULL;
	}

	self->info = cc_map_new();
	if(self->info == NULL)
	{
		goto fail_info;
	}

	if(xml_istream_parse((void*) self,
	                     osmdb_filter_start, osmdb_filter_end,
	                     fname) == 0)
	{
		goto fail_parse;
	}

	// success
	return self;

	// failure
	fail_parse:
		osmdb_filter_discard(self);
		cc_map_delete(&self->info);
	fail_info:
		FREE(self);
	return NULL;
}

void osmdb_filter_delete(osmdb_filter_t** _self)
{
	ASSERT(_self);

	osmdb_filter_t* self = *_self;
	if(self)
	{
		osmdb_filter_discard(self);
		cc_map_delete(&self->info);
		FREE(self);
		*_self = NULL;
	}
}

osmdb_filterInfo_t*
osmdb_filter_selectNode(osmdb_filter_t* self,
                        osmdb_node_t* node)
{
	ASSERT(self);
	ASSERT(node);

	const char* class = osmdb_classCodeToName(node->class);

	cc_mapIter_t iter;
	osmdb_filterInfo_t* info;
	info = (osmdb_filterInfo_t*)
	       cc_map_find(self->info, &iter, class);
	if(info == NULL)
	{
		return NULL;
	}

	// reject unnamed nodes
	if(info->named && (node->name == NULL))
	{
		return NULL;
	}

	return info;
}

osmdb_filterInfo_t*
osmdb_filter_selectWay(osmdb_filter_t* self,
                       osmdb_way_t* way)
{
	ASSERT(self);
	ASSERT(way);

	const char* class = osmdb_classCodeToName(way->class);

	cc_mapIter_t iter;
	osmdb_filterInfo_t* info;
	info = (osmdb_filterInfo_t*)
	       cc_map_find(self->info, &iter, class);
	if(info == NULL)
	{
		return NULL;
	}

	// reject unnamed ways
	if(info->named && (way->name == NULL))
	{
		return NULL;
	}

	return info;
}

osmdb_filterInfo_t*
osmdb_filter_selectRelation(osmdb_filter_t* self,
                            osmdb_relation_t* relation)
{
	ASSERT(self);
	ASSERT(relation);

	const char* class = osmdb_classCodeToName(relation->class);

	cc_mapIter_t iter;
	osmdb_filterInfo_t* info;
	info = (osmdb_filterInfo_t*)
	       cc_map_find(self->info, &iter, class);
	if(info == NULL)
	{
		return NULL;
	}

	// reject unnamed relations
	if(info->named && (relation->name == NULL))
	{
		return NULL;
	}

	return info;
}

osmdb_filterInfo_t*
osmdb_filter_selectClass(osmdb_filter_t* self,
                         int class_code)
{
	ASSERT(self);

	const char* class = osmdb_classCodeToName(class_code);

	cc_mapIter_t iter;
	return (osmdb_filterInfo_t*)
	       cc_map_find(self->info, &iter, class);
}

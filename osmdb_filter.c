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
#include "libxmlstream/xml_istream.h"
#include "osmdb_filter.h"
#include "osmdb_util.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

#define OSMDB_CLASS_SELECTED 1

/***********************************************************
* private                                                  *
***********************************************************/

typedef struct
{
	int named;
	a3d_list_t* levels;
} osmdb_filterMask_t;

static osmdb_filterMask_t* osmdb_filterMask_new(void)
{
	osmdb_filterMask_t* self = (osmdb_filterMask_t*)
	                           malloc(sizeof(osmdb_filterMask_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->levels = a3d_list_new();
	if(self->levels == NULL)
	{
		goto fail_levels;
	}

	self->named = 0;

	// success
	return self;

	// failure
	fail_levels:
		free(self);
	return NULL;
}

static void osmdb_filterMask_delete(osmdb_filterMask_t** _self)
{
	assert(_self);

	osmdb_filterMask_t* self = *_self;
	if(self)
	{
		a3d_list_discard(self->levels);
		a3d_list_delete(&self->levels);
		free(self);
		*_self = NULL;
	}
}

static int osmdb_filter_start(void* priv,
                              int line,
                              const char* name,
                              const char** atts)
{
	assert(priv);
	assert(name);
	assert(atts);

	osmdb_filter_t* self = (osmdb_filter_t*) priv;

	if(strcmp(name, "select") != 0)
	{
		return 1;
	}

	const char* class  = NULL;
	const char* levels = NULL;
	const char* named  = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		else if(strcmp(atts[idx0], "levels") == 0)
		{
			levels = atts[idx1];
		}
		else if(strcmp(atts[idx0], "named") == 0)
		{
			named = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((class  == NULL) &&
	   (levels == NULL))
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	// check for duplicates
	a3d_hashmapIter_t iterator;
	if(a3d_hashmap_find(self->masks, &iterator, class))
	{
		LOGE("duplicate line=%i", line);
		return 0;
	}

	// create filter mask
	osmdb_filterMask_t* fm = osmdb_filterMask_new();
	if(fm == NULL)
	{
		return 0;
	}

	// parse levels
	int  zoom;
	char str[256];
	int  src = 0;
	int  dst = 0;
	while(1)
	{
		str[dst] = levels[src];
		if((str[dst] == ',') || (str[dst] == '\0'))
		{
			// parse the zoom level
			str[dst] = '\0';
			zoom     = (int) strtol(str, NULL, 0);

			// add zoom to levels
			if(a3d_list_append(fm->levels, NULL,
			                   (const void*) zoom) == NULL)
			{
				goto fail_add;
			}

			// end of string
			if(levels[src] == '\0')
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

	// check if the named flag is set
	if(named)
	{
		fm->named = (int) strtol(named, NULL, 0);
	}

	// add the filter mask
	if(a3d_hashmap_add(self->masks, &iterator,
	                   (const void*) fm,
	                   class) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_filterMask_delete(&fm);
	return 0;
}

static int osmdb_filter_end(void* priv,
                            int line,
                            const char* name,
                            const char* content)
{
	// content may be NULL
	assert(priv);
	assert(name);

	return 1;
}

static void osmdb_filter_discard(osmdb_filter_t* self)
{
	assert(self);

	a3d_hashmapIter_t  iterator;
	a3d_hashmapIter_t* iter;
	iter = a3d_hashmap_head(self->masks, &iterator);
	while(iter)
	{
		osmdb_filterMask_t* fm = (osmdb_filterMask_t*)
		                         a3d_hashmap_remove(self->masks,
		                                            &iter);
		osmdb_filterMask_delete(&fm);
	}
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_filter_t* osmdb_filter_new(const char* fname)
{
	assert(fname);

	osmdb_filter_t* self = (osmdb_filter_t*)
	                       malloc(sizeof(osmdb_filter_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->masks = a3d_hashmap_new();
	if(self->masks == NULL)
	{
		goto fail_masks;
	}

	if(xml_istream_parse((void*) self,
	                     osmdb_filter_start,
	                     osmdb_filter_end,
	                     fname) == 0)
	{
		goto fail_parse;
	}

	// success
	return self;

	// failure
	fail_parse:
		osmdb_filter_discard(self);
		a3d_hashmap_delete(&self->masks);
	fail_masks:
		free(self);
	return NULL;
}

void osmdb_filter_delete(osmdb_filter_t** _self)
{
	assert(_self);

	osmdb_filter_t* self = *_self;
	if(self)
	{
		osmdb_filter_discard(self);
		a3d_hashmap_delete(&self->masks);
		free(self);
		*_self = NULL;
	}
}

a3d_list_t* osmdb_filter_selectNode(osmdb_filter_t* self,
                                    osmdb_node_t* node)
{
	assert(self);
	assert(node);

	const char* class = osmdb_classCodeToName(node->class);

	a3d_hashmapIter_t iter;
	osmdb_filterMask_t* fm = (osmdb_filterMask_t*)
	                         a3d_hashmap_find(self->masks,
	                                          &iter, class);
	if(fm == NULL)
	{
		return NULL;
	}

	// reject unnamed nodes
	if(fm->named && (node->name == NULL))
	{
		return NULL;
	}

	return fm->levels;
}

a3d_list_t* osmdb_filter_selectWay(osmdb_filter_t* self,
                                   osmdb_way_t* way)
{
	assert(self);
	assert(way);

	const char* class = osmdb_classCodeToName(way->class);

	a3d_hashmapIter_t iter;
	osmdb_filterMask_t* fm = (osmdb_filterMask_t*)
	                         a3d_hashmap_find(self->masks,
	                                          &iter, class);
	if(fm == NULL)
	{
		return NULL;
	}

	// reject unnamed ways
	if(fm->named && (way->name == NULL))
	{
		return NULL;
	}

	return fm->levels;
}

a3d_list_t* osmdb_filter_selectRelation(osmdb_filter_t* self,
                                        osmdb_relation_t* relation)
{
	assert(self);
	assert(relation);

	const char* class = osmdb_classCodeToName(relation->class);

	a3d_hashmapIter_t iter;
	osmdb_filterMask_t* fm = (osmdb_filterMask_t*)
	                         a3d_hashmap_find(self->masks,
	                                          &iter, class);
	if(fm == NULL)
	{
		return NULL;
	}

	// reject unnamed relations
	if(fm->named && (relation->name == NULL))
	{
		return NULL;
	}

	return fm->levels;
}

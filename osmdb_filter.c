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
#include "osmdb_filter.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

#define OSMDB_CLASS_SELECTED 1

/***********************************************************
* private                                                  *
***********************************************************/

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

	const char* class = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(class == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	// check for duplicates
	a3d_hashmapIter_t iterator;
	if(a3d_hashmap_find(self->classes, &iterator, class))
	{
		LOGE("duplicate line=%i", line);
		return 0;
	}

	// add the class
	if(a3d_hashmap_add(self->classes, &iterator,
	                   (const void*) &OSMDB_CLASS_SELECTED,
	                   class) == 0)
	{
		return 0;
	}

	return 1;
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

	a3d_hashmap_t* self->classes = a3d_hashmap_new();
	if(self->classes == NULL)
	{
		goto fail_classes;
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
		a3d_hashmap_discard(self->classes);
		a3d_hashmap_delete(&self->classes);
	fail_classes:
		free(self);
	return NULL;
}

void osmdb_filter_delete(osmdb_filter_t** _self)
{
	assert(_self);

	osmdb_filter_t* self = *_self;
	if(self)
	{
		a3d_hashmap_discard(self->classes);
		a3d_hashmap_delete(&self->classes);
		free(self);
		*_self = NULL;
	}
}

int osmdb_filter_select(osmdb_filter_t* self,
                        const char** atts, int line)
{
	assert(self);
	assert(atts);

	const char* class = NULL;

	// find atts
	int idx0  = 0;
	int idx1  = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// replace null string with none
	const char* none = "class:none";
	if(class == NULL)
	{
		class = none;
	}

	a3d_hashmapIter_t iter;
	return a3d_hashmap_find(self->classes, &iter, class);
}

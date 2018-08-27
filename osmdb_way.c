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
#include "osmdb_way.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_way_t* osmdb_way_new(const char** atts, int line)
{
	assert(atts);

	const char* id    = NULL;
	const char* name  = NULL;
	const char* abrev = NULL;
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

	self->id = strtod(id, NULL);

	if(name)
	{
		int len = strlen(name) + 1;
		self->name = (char*) malloc(len*sizeof(char));
		if(self->name == NULL)
		{
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
			goto fail_abrev;
		}
		snprintf(self->abrev, len, "%s", abrev);
	}

	if(class)
	{
		self->class = osmdb_classNameToCode(class);
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

void osmdb_way_delete(osmdb_way_t** _self)
{
	assert(_self);

	osmdb_way_t* self = *_self;
	if(self)
	{
		a3d_list_delete(&self->nds);
		free(self->name);
		free(self->abrev);
		free(self);
		*_self = NULL;
	}
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

	// create the nd
	double* _ref = (double*) malloc(sizeof(double));
	if(_ref == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	*_ref = strtod(ref);

	// add nd to list
	if(a3d_list_append(self->nds, NULL,
	                   (const void*) _ref) == NULL)
	{
		goto fail_append:
	}

	// success
	return 1;

	// failure
	fail_append:
		free(_ref);
	return 0;
}

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
#include "a3d/a3d_hashmap.h"
#include "osmdb_parser.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

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

	if(strcmp(name, "select") != 0)
	{
		return 1;
	}

	a3d_hashmap_t* self = (a3d_hashmap_t*) priv;

	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "class") == 0)
		{
			// copy class
			int len = strlen(atts[idx1]) + 1;
			char* s = (char*) malloc(len*sizeof(char));
			if(s == NULL)
			{
				return 0;
			}
			snprintf(s, len, "%s", atts[idx1]);

			a3d_hashmapIter_t iter;
			if(a3d_hashmap_add(self, &iter, s, s) == 0)
			{
				free(s);
				return 0;
			}
			return 1;
		}
		idx0 += 2;
		idx1 += 2;
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

static void osmdb_filter_delete(a3d_hashmap_t** _self)
{
	assert(_self);

	a3d_hashmap_t* self = *_self;
	if(self)
	{
		a3d_hashmapIter_t  iterator;
		a3d_hashmapIter_t* iter;
		iter = a3d_hashmap_head(self, &iterator);
		while(iter)
		{
			char* s = (char*) a3d_hashmap_remove(self, &iter);
			free(s);
		}
		a3d_hashmap_delete(_self);
	}
}

static a3d_hashmap_t* osmdb_filter_new(const char* fname)
{
	assert(fname);

	a3d_hashmap_t* self = a3d_hashmap_new();
	if(self == NULL)
	{
		return NULL;
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
		osmdb_filter_delete(&self);
	return NULL;
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	if(argc != 4)
	{
		LOGE("%s filter.xml in.osmdb.gz out.osmdb.gz", argv[0]);
		return EXIT_FAILURE;
	}

	const char* fname_filter = argv[1];
	const char* fname_in     = argv[2];
	const char* fname_out    = argv[3];

	// import class filter
	a3d_hashmap_t* classes = osmdb_filter_new(fname_filter);
	if(classes == NULL)
	{
		return EXIT_FAILURE;
	}

	osmdb_parser_t* parser;
	parser = osmdb_parser_new(classes, fname_out);
	if(parser == NULL)
	{
		goto fail_parser;
	}

	LOGI("PARSING RELATIONS");
	osmdb_parser_mode(parser, OSMDB_MODE_REL);
	if(xml_istream_parseGz(parser,
	                       osmdb_parser_start,
	                       osmdb_parser_end,
	                       fname_in) == 0)
	{
		LOGE("fail relations");
		goto fail_relations;
	}

	LOGI("PARSING WAYS");
	osmdb_parser_mode(parser, OSMDB_MODE_WAY);
	if(xml_istream_parseGz(parser,
	                       osmdb_parser_start,
	                       osmdb_parser_end,
	                       fname_in) == 0)
	{
		LOGE("fail ways");
		goto fail_ways;
	}

	LOGI("PARSING NODES");
	osmdb_parser_mode(parser, OSMDB_MODE_WRITE);
	if(xml_istream_parseGz(parser,
	                       osmdb_parser_start,
	                       osmdb_parser_end,
	                       fname_in) == 0)
	{
		LOGE("fail nodes");
		goto fail_nodes;
	}

	osmdb_parser_delete(&parser);
	osmdb_filter_delete(&classes);

	// success
	return EXIT_SUCCESS;

	// failure
	fail_nodes:
	fail_ways:
	fail_relations:
		osmdb_parser_delete(&parser);
	fail_parser:
		osmdb_filter_delete(&classes);
	return EXIT_FAILURE;
}

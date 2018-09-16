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
#include <math.h>

#define LOG_TAG "osmdb"
#include "a3d/a3d_timestamp.h"
#include "libxmlstream/xml_log.h"
#include "../osmdb_parser.h"
#include "../osmdb_node.h"
#include "../osmdb_way.h"
#include "../osmdb_relation.h"
#include "../osmdb_index.h"

/***********************************************************
* private                                                  *
***********************************************************/

double stats_nodes     = 0.0;
double stats_ways      = 0.0;
double stats_relations = 0.0;

static int nodeFn(void* priv, osmdb_node_t* node)
{
	assert(priv);
	assert(node);

	osmdb_index_t* index = (osmdb_index_t*) priv;

	stats_nodes += 1.0;
	if(fmod(stats_nodes, 100000.0) == 0.0)
	{
		osmdb_index_stats(index);
	}

	return osmdb_index_addChunk(index, OSMDB_TYPE_NODE,
	                            (const void*) node);
}

static int wayFn(void* priv, osmdb_way_t* way)
{
	assert(priv);
	assert(way);

	osmdb_index_t* index = (osmdb_index_t*) priv;

	stats_ways += 1.0;
	if(fmod(stats_ways, 100000.0) == 0.0)
	{
		osmdb_index_stats(index);
	}

	return osmdb_index_addChunk(index, OSMDB_TYPE_WAY,
	                            (const void*) way);
}

static int relationFn(void* priv, osmdb_relation_t* relation)
{
	assert(priv);
	assert(relation);

	osmdb_index_t* index = (osmdb_index_t*) priv;

	stats_relations += 1.0;
	if(fmod(stats_relations, 100000.0) == 0.0)
	{
		osmdb_index_stats(index);
	}

	return osmdb_index_addChunk(index, OSMDB_TYPE_RELATION,
	                            (const void*) relation);
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	double t0 = a3d_timestamp();

	if(argc != 3)
	{
		LOGE("%s in.osmdb.gz out-path", argv[0]);
		return EXIT_FAILURE;
	}

	const char* fname = argv[1];
	const char* path  = argv[2];

	osmdb_index_t* index = osmdb_index_new(path);
	if(index == NULL)
	{
		return EXIT_FAILURE;
	}

	if(osmdb_parse(fname, (void*) index,
	               nodeFn, wayFn, relationFn) == 0)
	{
		goto fail_parse;
	}

	if(osmdb_index_delete(&index) == 0)
	{
		LOGE("FAILURE dt=%lf", a3d_timestamp() - t0);
		return EXIT_FAILURE;
	}

	// success
	LOGE("SUCCESS dt=%lf", a3d_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_parse:
		osmdb_index_delete(&index);
	LOGE("FAILURE dt=%lf", a3d_timestamp() - t0);
	return EXIT_FAILURE;
}

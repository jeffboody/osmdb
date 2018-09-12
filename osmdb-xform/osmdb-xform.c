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
#include <math.h>

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"
#include "../osmdb_node.h"
#include "../osmdb_way.h"
#include "../osmdb_relation.h"
#include "../osmdb_index.h"
#include "../osmdb_filter.h"

/***********************************************************
* private - args                                           *
***********************************************************/

typedef struct
{
	int         indexed;
	const char* fname;
	const char* input;
	const char* output;
} osmdb_Args_t;

static int parseArgs(int argc, const char** argv,
                     osmdb_Args_t* args)
{
	assert(args);

	// initialize args
	args->indexed = 1;
	args->fname   = NULL;
	args->input   = NULL;
	args->output  = NULL;

	// parse args
	int i = 1;
	while(i < argc)
	{
		if(strcmp(argv[i], "-tile") == 0)
		{
			args->indexed = 0;
		}
		else if(strcmp(argv[i], "-f") == 0)
		{
			++i;
			if(i >= argc)
			{
				return 0;
			}

			args->fname = argv[i];
		}
		else if(args->input == NULL)
		{
			args->input = argv[i];
		}
		else if(args->output == NULL)
		{
			args->output = argv[i];
		}
		else
		{
			return 0;
		}

		++i;
	}

	// check required args
	if((args->input  == NULL) ||
	   (args->output == NULL))
	{
		return 0;
	}

	return 1;
}

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_addNodeCopy(osmdb_node_t* node,
                  osmdb_index_t* iindex,
                  osmdb_index_t* oindex)
{
	assert(node);
	assert(iindex);
	assert(oindex);

	osmdb_node_t* copy = osmdb_node_copy(node);
	if(copy == NULL)
	{
		return 0;
	}

	if(osmdb_index_add(oindex, OSMDB_TYPE_NODE,
	                   (const void*) copy) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_node_delete(&copy);
	return 0;
}

static int
osmdb_addWayNodes(osmdb_way_t* way,
                  osmdb_index_t* iindex,
                  osmdb_index_t* oindex)
{
	assert(way);
	assert(iindex);
	assert(oindex);

	a3d_listitem_t* iter = a3d_list_head(way->nds);
	while(iter)
	{
		double*       ref = (double*) a3d_list_peekitem(iter);
		osmdb_node_t* nd  = (osmdb_node_t*)
		                    osmdb_index_find(iindex,
		                                     OSMDB_TYPE_NODE,
		                                     *ref);
		if(nd == NULL)
		{
			// assume node was cropped by osmosis
			iter = a3d_list_next(iter);
			continue;
		}

		if(osmdb_addNodeCopy(nd, iindex, oindex) == 0)
		{
			return 0;
		}

		iter = a3d_list_next(iter);
	}

	return 1;
}

static int
osmdb_addWayCopy(osmdb_way_t* way,
                 osmdb_index_t* iindex,
                 osmdb_index_t* oindex)
{
	assert(way);
	assert(iindex);
	assert(oindex);

	osmdb_way_t* copy = osmdb_way_copy(way);
	if(copy == NULL)
	{
		return 0;
	}

	if(osmdb_addWayNodes(copy, iindex, oindex) == 0)
	{
		goto fail_add;
	}

	if(osmdb_index_add(oindex, OSMDB_TYPE_WAY,
	                   (const void*) copy) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_way_delete(&copy);
	return 0;
}

static int
osmdb_addRelationMembers(osmdb_relation_t* relation,
                         osmdb_index_t* iindex,
                         osmdb_index_t* oindex)
{
	assert(relation);
	assert(iindex);
	assert(oindex);

	a3d_listitem_t* iter = a3d_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    a3d_list_peekitem(iter);
		if(m->type == OSMDB_TYPE_NODE)
		{
			osmdb_node_t* nd  = (osmdb_node_t*)
			                    osmdb_index_find(iindex,
			                                     OSMDB_TYPE_NODE,
			                                     m->ref);
			if(nd == NULL)
			{
				// assume node was cropped by osmosis
				iter = a3d_list_next(iter);
				continue;
			}

			if(osmdb_addNodeCopy(nd, iindex, oindex) == 0)
			{
				return 0;
			}
		}
		else if(m->type == OSMDB_TYPE_WAY)
		{
			osmdb_way_t* way = (osmdb_way_t*)
			                    osmdb_index_find(iindex,
			                                     OSMDB_TYPE_WAY,
			                                     m->ref);
			if(way == NULL)
			{
				// assume way was cropped by osmosis
				iter = a3d_list_next(iter);
				continue;
			}

			if(osmdb_addWayCopy(way, iindex, oindex) == 0)
			{
				return 0;
			}
		}

		iter = a3d_list_next(iter);
	}

	return 1;
}

static int
osmdb_addRelationCopy(osmdb_relation_t* relation,
                      osmdb_index_t* iindex,
                      osmdb_index_t* oindex)
{
	assert(relation);
	assert(iindex);
	assert(oindex);

	osmdb_relation_t* copy = osmdb_relation_copy(relation);
	if(copy == NULL)
	{
		return 0;
	}

	if(osmdb_addRelationMembers(copy, iindex, oindex) == 0)
	{
		goto fail_add;
	}

	if(osmdb_index_add(oindex, OSMDB_TYPE_RELATION,
	                   (const void*) copy) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_relation_delete(&copy);
	return 0;
}

static int osmdb_xform(osmdb_filter_t* filter,
                       osmdb_index_t* iindex,
                       osmdb_index_t* oindex)
{
	// filter is optional
	assert(iindex);
	assert(oindex);

	// xform nodes
	osmdb_indexIter_t* iter;
	double cnt = 0.0;
	iter = osmdb_indexIter_new(iindex, OSMDB_TYPE_NODE);
	while(iter)
	{
		cnt += 1.0;
		if(fmod(cnt, 100000.0) == 0.0)
		{
			LOGI("[N] %0.0lf", cnt);
			osmdb_index_stats(oindex);
			osmdb_index_stats(iindex);
		}

		osmdb_node_t* node = (osmdb_node_t*)
		                     osmdb_indexIter_peek(iter);

		if(filter)
		{
			if(osmdb_filter_selectNode(filter, node) == 0)
			{
				iter = osmdb_indexIter_next(iter);
				continue;
			}
		}

		osmdb_node_t* copy = osmdb_node_copy(node);
		if(copy == NULL)
		{
			goto fail_add;
		}

		if(osmdb_index_add(oindex, OSMDB_TYPE_NODE,
		                   (const void*) copy) == 0)
		{
			osmdb_node_delete(&copy);
			goto fail_add;
		}
		iter = osmdb_indexIter_next(iter);
	}
	LOGI("[N] %0.0lf", cnt);
	osmdb_index_stats(oindex);
	osmdb_index_stats(iindex);

	// xform ways
	cnt = 0.0;
	iter = osmdb_indexIter_new(iindex, OSMDB_TYPE_WAY);
	while(iter)
	{
		cnt += 1.0;
		if(fmod(cnt, 100000.0) == 0.0)
		{
			LOGI("[W] %0.0lf", cnt);
			osmdb_index_stats(oindex);
			osmdb_index_stats(iindex);
		}

		osmdb_way_t* way = (osmdb_way_t*)
		                    osmdb_indexIter_peek(iter);

		if(filter)
		{
			if(osmdb_filter_selectWay(filter, way) == 0)
			{
				iter = osmdb_indexIter_next(iter);
				continue;
			}
		}

		if(osmdb_addWayCopy(way, iindex, oindex) == 0)
		{
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
	}
	LOGI("[W] %0.0lf", cnt);
	osmdb_index_stats(oindex);
	osmdb_index_stats(iindex);

	// xform relations
	cnt = 0.0;
	iter = osmdb_indexIter_new(iindex, OSMDB_TYPE_RELATION);
	while(iter)
	{
		cnt += 1.0;
		if(fmod(cnt, 100000.0) == 0.0)
		{
			LOGI("[R] %0.0lf", cnt);
			osmdb_index_stats(oindex);
			osmdb_index_stats(iindex);
		}

		osmdb_relation_t* relation = (osmdb_relation_t*)
		                             osmdb_indexIter_peek(iter);

		if(filter)
		{
			if(osmdb_filter_selectRelation(filter, relation) == 0)
			{
				iter = osmdb_indexIter_next(iter);
				continue;
			}
		}

		if(osmdb_addRelationCopy(relation, iindex, oindex) == 0)
		{
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
	}
	LOGI("[R] %0.0lf", cnt);
	osmdb_index_stats(oindex);
	osmdb_index_stats(iindex);

	// success
	return 1;

	// failure
	fail_add:
		osmdb_indexIter_delete(&iter);
	return 0;
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, const char** argv)
{
	osmdb_Args_t args;
	if(parseArgs(argc, argv, &args) == 0)
	{
		LOGE("%s [-tile] [-f filter.xml] input output", argv[0]);
		return EXIT_FAILURE;
	}

	// load the optional filter
	osmdb_filter_t* filter = NULL;
	if(args.fname)
	{
		filter = osmdb_filter_new(args.fname);
		if(filter == NULL)
		{
			LOGE("FAILURE");
			return EXIT_FAILURE;
		}
	}

	osmdb_index_t* iindex = osmdb_index_new(args.input);
	if(iindex == NULL)
	{
		goto fail_iindex;
	}

	osmdb_index_t* oindex = osmdb_index_new(args.output);
	if(oindex == NULL)
	{
		goto fail_oindex;
	}

	if(osmdb_xform(filter, iindex, oindex) == 0)
	{
		goto fail_xform;
	}

	if(osmdb_index_delete(&oindex) == 0)
	{
		goto fail_delete_oindex;
	}

	if(osmdb_index_delete(&iindex) == 0)
	{
		goto fail_delete_iindex;
	}

	osmdb_filter_delete(&filter);

	// success
	LOGI("SUCCESS");
	return EXIT_SUCCESS;

	// failure
	fail_delete_iindex:
	fail_delete_oindex:
	fail_xform:
		osmdb_index_delete(&oindex);
	fail_oindex:
		osmdb_index_delete(&iindex);
	fail_iindex:
		osmdb_filter_delete(&filter);
		LOGE("FAILURE");
	return EXIT_FAILURE;
}

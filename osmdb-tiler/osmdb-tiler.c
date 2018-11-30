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

#include "../osmdb_index.h"
#include "../osmdb_filter.h"
#include "../osmdb_node.h"
#include "../osmdb_way.h"
#include "../osmdb_relation.h"
#include "a3d/a3d_timestamp.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* private                                                  *
***********************************************************/

static int osmdb_tiler(osmdb_filter_t* filter,
                       osmdb_index_t* index)
{
	assert(filter);
	assert(index);

	// tile nodes
	osmdb_indexIter_t* iter;
	double cnt = 0.0;
	iter = osmdb_indexIter_new(index, OSMDB_TYPE_NODE);
	while(iter)
	{
		cnt += 1.0;
		if(fmod(cnt, 100000.0) == 0.0)
		{
			LOGI("[N] %0.0lf", cnt);
			osmdb_index_stats(index);
		}

		osmdb_node_t* node = (osmdb_node_t*)
		                     osmdb_indexIter_peek(iter);

		osmdb_filterInfo_t* info;
		info  = osmdb_filter_selectNode(filter, node);
		if(info == NULL)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addNode(&range, node);
		if(osmdb_index_addTile(index, &range, info->zoom,
		                       OSMDB_TYPE_NODE,
		                       node->id) == 0)
		{
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
	}
	LOGI("[N] %0.0lf", cnt);
	osmdb_index_stats(index);

	// tile ways
	cnt = 0.0;
	iter = osmdb_indexIter_new(index, OSMDB_TYPE_WAY);
	while(iter)
	{
		cnt += 1.0;
		if(fmod(cnt, 100000.0) == 0.0)
		{
			LOGI("[W] %0.0lf", cnt);
			osmdb_index_stats(index);
		}

		osmdb_way_t* way = (osmdb_way_t*)
		                    osmdb_indexIter_peek(iter);

		osmdb_filterInfo_t* info;
		info  = osmdb_filter_selectWay(filter, way);
		if(info == NULL)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addWay(&range, index, way);
		if(osmdb_index_addTile(index, &range, info->zoom,
		                       OSMDB_TYPE_WAY,
		                       way->id) == 0)
		{
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
	}
	LOGI("[W] %0.0lf", cnt);
	osmdb_index_stats(index);

	// tile relations
	cnt = 0.0;
	iter = osmdb_indexIter_new(index, OSMDB_TYPE_RELATION);
	while(iter)
	{
		cnt += 1.0;
		if(fmod(cnt, 100000.0) == 0.0)
		{
			LOGI("[R] %0.0lf", cnt);
			osmdb_index_stats(index);
		}

		osmdb_relation_t* relation = (osmdb_relation_t*)
		                             osmdb_indexIter_peek(iter);

		osmdb_filterInfo_t* info;
		info  = osmdb_filter_selectRelation(filter, relation);
		if(info == NULL)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addRelation(&range, index, relation);
		if(osmdb_index_addTile(index, &range, info->zoom,
		                       OSMDB_TYPE_RELATION,
		                       relation->id) == 0)
		{
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
	}
	LOGI("[R] %0.0lf", cnt);
	osmdb_index_stats(index);

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
	double t0 = a3d_timestamp();

	if(argc != 3)
	{
		LOGE("%s [filter.xml] [prefix]", argv[0]);
		return EXIT_FAILURE;
	}

	// load the filter
	osmdb_filter_t* filter = osmdb_filter_new(argv[1]);
	if(filter == NULL)
	{
		LOGI("FAILURE dt=%lf", a3d_timestamp() - t0);
		return EXIT_FAILURE;
	}

	char path_index[256];
	snprintf(path_index, 256, "%s-index", argv[2]);
	osmdb_index_t* index = osmdb_index_new(path_index);
	if(index == NULL)
	{
		goto fail_index;
	}

	if(osmdb_tiler(filter, index) == 0)
	{
		goto fail_tiler;
	}

	if(osmdb_index_delete(&index) == 0)
	{
		goto fail_delete_index;
	}

	osmdb_filter_delete(&filter);

	// success
	LOGI("SUCCESS dt=%lf", a3d_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_delete_index:
	fail_tiler:
		osmdb_index_delete(&index);
	fail_index:
		osmdb_filter_delete(&filter);
		LOGI("FAILURE dt=%lf", a3d_timestamp() - t0);
	return EXIT_FAILURE;
}

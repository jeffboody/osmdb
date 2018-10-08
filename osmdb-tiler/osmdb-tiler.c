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
#include "a3d/a3d_timestamp.h"
#include "libxmlstream/xml_log.h"
#include "terrain/terrain_util.h"
#include "../osmdb_node.h"
#include "../osmdb_way.h"
#include "../osmdb_relation.h"
#include "../osmdb_index.h"
#include "../osmdb_filter.h"

/***********************************************************
* private                                                  *
***********************************************************/

typedef struct
{
	int    pts;
	double latT;
	double lonL;
	double latB;
	double lonR;
} osmdb_range_t;

static void osmdb_range_init(osmdb_range_t* self)
{
	assert(self);

	self->pts  = 0;
	self->latT = 0.0;
	self->lonL = 0.0;
	self->latB = 0.0;
	self->lonR = 0.0;
}

static void osmdb_range_addNode(osmdb_range_t* self,
                                osmdb_node_t* node)
{
	assert(self);
	assert(node);

	if(self->pts)
	{
		if(node->lat > self->latT)
		{
			self->latT = node->lat;
		}
		else if(node->lat < self->latB)
		{
			self->latB = node->lat;
		}

		if(node->lon < self->lonL)
		{
			self->lonL = node->lon;
		}
		else if(node->lon > self->lonR)
		{
			self->lonR = node->lon;
		}
	}
	else
	{
		self->latT = node->lat;
		self->lonL = node->lon;
		self->latB = node->lat;
		self->lonR = node->lon;
	}
	++self->pts;
}

static void osmdb_range_addWay(osmdb_range_t* self,
                               osmdb_index_t* index,
                               osmdb_way_t* way)
{
	assert(self);
	assert(index);
	assert(way);

	osmdb_node_t* node;

	a3d_listitem_t* iter = a3d_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_peekitem(iter);
		node = (osmdb_node_t*)
		       osmdb_index_find(index,
		                        OSMDB_TYPE_NODE,
		                        *ref);
		if(node)
		{
			osmdb_range_addNode(self, node);
		}

		iter = a3d_list_next(iter);
	}
}

static void osmdb_range_addRelation(osmdb_range_t* self,
                                    osmdb_index_t* index,
                                    osmdb_relation_t* relation)
{
	assert(self);
	assert(index);
	assert(relation);

	osmdb_node_t* node;
	osmdb_way_t*  way;

	a3d_listitem_t* iter = a3d_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    a3d_list_peekitem(iter);
		if(m->type == OSMDB_TYPE_NODE)
		{
			node = (osmdb_node_t*)
			       osmdb_index_find(index,
			                        m->type,
			                        m->ref);
			if(node)
			{
				osmdb_range_addNode(self, node);
			}
		}
		else if(m->type == OSMDB_TYPE_WAY)
		{
			way = (osmdb_way_t*)
			       osmdb_index_find(index,
			                        m->type,
			                        m->ref);
			if(way)
			{
				osmdb_range_addWay(self, index, way);
			}
		}
		// ignore relation members

		iter = a3d_list_next(iter);
	}
}

static int osmdb_range_addTile(osmdb_range_t* self,
                               a3d_list_t* levels,
                               osmdb_index_t* index,
		                       int type, double id)
{
	assert(self);
	assert(levels);
	assert(index);

	// ignore null range
	if(self->pts == 0)
	{
		return 1;
	}

	int ret = 1;
	a3d_listitem_t* iter = a3d_list_head(levels);
	while(iter)
	{
		int zoom = (int) a3d_list_peekitem(iter);

		// compute the range
		float x0f;
		float y0f;
		float x1f;
		float y1f;
		terrain_coord2tile(self->latT, self->lonL,
		                   zoom, &x0f, &y0f);
		terrain_coord2tile(self->latB, self->lonR,
		                   zoom, &x1f, &y1f);

		// add id to range
		int x;
		int y;
		int x0 = (int) x0f;
		int x1 = (int) x1f;
		int y0 = (int) y0f;
		int y1 = (int) y1f;
		for(y = y0; y <= y1; ++y)
		{
			for(x = x0; x <= x1; ++x)
			{
				ret &= osmdb_index_addTile(index, zoom, x, y,
				                           type, id);
			}
		}

		iter = a3d_list_next(iter);
	}
	return ret;
}

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

		a3d_list_t* levels = osmdb_filter_selectNode(filter, node);
		if(levels == NULL)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addNode(&range, node);
		if(osmdb_range_addTile(&range, levels, index,
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

		a3d_list_t* levels = osmdb_filter_selectWay(filter, way);
		if(levels == NULL)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addWay(&range, index, way);
		if(osmdb_range_addTile(&range, levels, index,
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

		a3d_list_t* levels = osmdb_filter_selectRelation(filter, relation);
		if(levels == NULL)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addRelation(&range, index, relation);
		if(osmdb_range_addTile(&range, levels, index,
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
		LOGE("%s filter.xml base", argv[0]);
		return EXIT_FAILURE;
	}
	const char* fname  = argv[1];
	const char* base   = argv[2];

	// load the filter
	osmdb_filter_t* filter = osmdb_filter_new(fname);
	if(filter == NULL)
	{
		LOGI("FAILURE dt=%lf", a3d_timestamp() - t0);
		return EXIT_FAILURE;
	}

	osmdb_index_t* index = osmdb_index_new(base);
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

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
#include "../osmdb_util.h"
#include "../osmdb_index.h"
#include "../osmdb_chunk.h"
#include "../osmdb_filter.h"

/***********************************************************
* private                                                  *
***********************************************************/

typedef struct
{
	osmdb_index_t*  index;
	osmdb_filter_t* filter;
} osmdb_indexer_t;

double stats_nodes     = 0.0;
double stats_ways      = 0.0;
double stats_relations = 0.0;

static int
osmdb_indexer_amendWays(osmdb_indexer_t* self)
{
	assert(self);

	osmdb_indexIter_t* iter;
	iter = osmdb_indexIter_new(self->index,
	                           OSMDB_TYPE_CTRWAY);
	if(iter == NULL)
	{
		return 0;
	}

	while(iter)
	{
		osmdb_way_t* way = (osmdb_way_t*)
		                   osmdb_indexIter_peek(iter);

		if(osmdb_index_find(self->index,
		                    OSMDB_TYPE_WAY,
		                    way->id))
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addWay(&range, self->index, way);
		if(range.pts == 0)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		stats_ways += 1.0;
		if(fmod(stats_ways, 100000.0) == 0.0)
		{
			LOGI("[W] %0.0lf", stats_ways);
			osmdb_index_stats(self->index);
		}

		double lat = (range.latT + range.latB)/2.0;
		double lon = (range.lonL + range.lonR)/2.0;
		osmdb_way_t* w = osmdb_way_copyCenter(way, lat, lon);
		if(w == NULL)
		{
			goto fail_add;
		}

		if(osmdb_index_addChunk(self->index,
		                        OSMDB_TYPE_WAY,
		                        (const void*) w) == 0)
		{
			osmdb_way_delete(&w);
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
		continue;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_indexIter_delete(&iter);
	return 0;
}

static int
osmdb_indexer_amendRelations(osmdb_indexer_t* self)
{
	assert(self);

	osmdb_indexIter_t* iter;
	iter = osmdb_indexIter_new(self->index,
	                           OSMDB_TYPE_CTRRELATION);
	if(iter == NULL)
	{
		return 0;
	}

	while(iter)
	{
		osmdb_relation_t* relation;
		relation = (osmdb_relation_t*)
		           osmdb_indexIter_peek(iter);

		if(osmdb_index_find(self->index,
		                    OSMDB_TYPE_RELATION,
		                    relation->id))
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addRelation(&range, self->index,
		                        relation);
		if(range.pts == 0)
		{
			iter = osmdb_indexIter_next(iter);
			continue;
		}

		stats_relations += 1.0;
		if(fmod(stats_relations, 100000.0) == 0.0)
		{
			LOGI("[R] %0.0lf", stats_relations);
			osmdb_index_stats(self->index);
		}

		double lat = (range.latT + range.latB)/2.0;
		double lon = (range.lonL + range.lonR)/2.0;
		osmdb_relation_t* r;
		r = osmdb_relation_copyCenter(relation, lat, lon);
		if(r == NULL)
		{
			goto fail_add;
		}

		if(osmdb_index_addChunk(self->index,
		                        OSMDB_TYPE_RELATION,
		                        (const void*) r) == 0)
		{
			osmdb_relation_delete(&r);
			goto fail_add;
		}

		iter = osmdb_indexIter_next(iter);
		continue;
	}

	// success
	return 1;

	// failure
	fail_add:
		osmdb_indexIter_delete(&iter);
	return 0;
}

static int nodeFn(void* priv, osmdb_node_t* node)
{
	assert(priv);
	assert(node);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;

	stats_nodes += 1.0;
	if(fmod(stats_nodes, 100000.0) == 0.0)
	{
		LOGI("[N] %0.0lf", stats_nodes);
		osmdb_index_stats(indexer->index);
	}

	osmdb_filterInfo_t* info;
	info = osmdb_filter_selectNode(indexer->filter, node);
	if(info ||
	   osmdb_index_find(indexer->index,
	                    OSMDB_TYPE_NODEREF, node->id))
	{
		return osmdb_index_addChunk(indexer->index,
		                            OSMDB_TYPE_NODE,
		                            (const void*) node);
	}
	else if(osmdb_index_find(indexer->index,
	                         OSMDB_TYPE_CTRNODEREF, node->id))
	{
		return osmdb_index_addChunk(indexer->index,
		                            OSMDB_TYPE_CTRNODE,
		                            (const void*) node);
	}

	osmdb_node_delete(&node);
	return 1;
}

static int wayFn(void* priv, osmdb_way_t* way)
{
	assert(priv);
	assert(way);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;

	stats_ways += 1.0;
	if(fmod(stats_ways, 100000.0) == 0.0)
	{
		LOGI("[W] %0.0lf", stats_ways);
		osmdb_index_stats(indexer->index);
	}

	osmdb_filterInfo_t* info;
	info = osmdb_filter_selectWay(indexer->filter, way);
	if((info && (info->center == 0)) ||
	   osmdb_index_find(indexer->index,
	                    OSMDB_TYPE_WAYREF, way->id))
	{
		if(osmdb_index_find(indexer->index,
		                    OSMDB_TYPE_WAY, way->id))
		{
			osmdb_way_delete(&way);
			return 1;
		}

		a3d_listitem_t* iter = a3d_list_head(way->nds);
		while(iter)
		{
			double* ref = (double*)
			              a3d_list_peekitem(iter);
			double* copy = (double*)
			               malloc(sizeof(double));
			if(copy == NULL)
			{
				LOGE("malloc failed");
				return 0;
			}
			*copy = *ref;

			if(osmdb_index_addChunk(indexer->index,
			                        OSMDB_TYPE_NODEREF,
			                        (const void*) copy) == 0)
			{
				free(copy);
				return 0;
			}

			iter = a3d_list_next(iter);
		}

		return osmdb_index_addChunk(indexer->index,
		                            OSMDB_TYPE_WAY,
		                            (const void*) way);
	}
	else if((info && (info->center)) ||
	        osmdb_index_find(indexer->index,
	                         OSMDB_TYPE_CTRWAYREF, way->id))
	{
		if(osmdb_index_find(indexer->index,
		                    OSMDB_TYPE_CTRWAY, way->id))
		{
			osmdb_way_delete(&way);
			return 1;
		}

		a3d_listitem_t* iter = a3d_list_head(way->nds);
		while(iter)
		{
			double* ref = (double*)
			              a3d_list_peekitem(iter);
			double* copy = (double*)
			               malloc(sizeof(double));
			if(copy == NULL)
			{
				LOGE("malloc failed");
				return 0;
			}
			*copy = *ref;

			if(osmdb_index_addChunk(indexer->index,
			                        OSMDB_TYPE_CTRNODEREF,
			                        (const void*) copy) == 0)
			{
				free(copy);
				return 0;
			}

			iter = a3d_list_next(iter);
		}

		return osmdb_index_addChunk(indexer->index,
		                            OSMDB_TYPE_CTRWAY,
		                            (const void*) way);
	}

	osmdb_way_delete(&way);
	return 1;
}

static int relationFn(void* priv, osmdb_relation_t* relation)
{
	assert(priv);
	assert(relation);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;

	stats_relations += 1.0;
	if(fmod(stats_relations, 100000.0) == 0.0)
	{
		LOGI("[R] %0.0lf", stats_relations);
		osmdb_index_stats(indexer->index);
	}

	osmdb_filterInfo_t* info;
	info = osmdb_filter_selectRelation(indexer->filter,
	                                   relation);
	if(info == NULL)
	{
		osmdb_relation_delete(&relation);
		return 1;
	}

	if(info->center)
	{
		int skip_ways = 0;
		a3d_listitem_t* iter = a3d_list_head(relation->members);
		while(iter)
		{
			osmdb_member_t* m = (osmdb_member_t*)
			                    a3d_list_peekitem(iter);
			if(m->type == OSMDB_TYPE_NODE)
			{
				double* copy = (double*)
				               malloc(sizeof(double));
				if(copy == NULL)
				{
					LOGE("malloc failed");
					return 0;
				}
				*copy = m->ref;

				if(osmdb_index_addChunk(indexer->index,
				                        OSMDB_TYPE_CTRNODEREF,
				                        (const void*) copy) == 0)
				{
					free(copy);
					return 0;
				}

				skip_ways = 1;
				break;
			}

			iter = a3d_list_next(iter);
		}

		if(skip_ways == 0)
		{
			iter = a3d_list_head(relation->members);
			while(iter)
			{
				osmdb_member_t* m = (osmdb_member_t*)
				                    a3d_list_peekitem(iter);
				if(m->type == OSMDB_TYPE_WAY)
				{
					double* copy = (double*)
					               malloc(sizeof(double));
					if(copy == NULL)
					{
						LOGE("malloc failed");
						return 0;
					}
					*copy = m->ref;

					if(osmdb_index_addChunk(indexer->index,
					                        OSMDB_TYPE_CTRWAYREF,
					                        (const void*) copy) == 0)
					{
						free(copy);
						return 0;
					}
				}

				iter = a3d_list_next(iter);
			}
		}

		return osmdb_index_addChunk(indexer->index,
		                            OSMDB_TYPE_CTRRELATION,
		                            (const void*) relation);
	}
	else
	{
		a3d_listitem_t* iter = a3d_list_head(relation->members);
		while(iter)
		{
			osmdb_member_t* m = (osmdb_member_t*)
			                    a3d_list_peekitem(iter);
			if(m->type == OSMDB_TYPE_NODE)
			{
				double* copy = (double*)
				               malloc(sizeof(double));
				if(copy == NULL)
				{
					LOGE("malloc failed");
					return 0;
				}
				*copy = m->ref;

				if(osmdb_index_addChunk(indexer->index,
				                        OSMDB_TYPE_NODEREF,
				                        (const void*) copy) == 0)
				{
					free(copy);
					return 0;
				}
			}
			else if(m->type == OSMDB_TYPE_WAY)
			{
				double* copy = (double*)
				               malloc(sizeof(double));
				if(copy == NULL)
				{
					LOGE("malloc failed");
					return 0;
				}
				*copy = m->ref;

				if(osmdb_index_addChunk(indexer->index,
				                        OSMDB_TYPE_WAYREF,
				                        (const void*) copy) == 0)
				{
					free(copy);
					return 0;
				}
			}

			iter = a3d_list_next(iter);
		}

		return osmdb_index_addChunk(indexer->index,
		                            OSMDB_TYPE_RELATION,
		                            (const void*) relation);
	}
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	double t0 = a3d_timestamp();

	if(argc != 3)
	{
		LOGE("%s [filter.xml] [prefix]", argv[0]);
		return EXIT_FAILURE;
	}

	char path_index[256];
	char fname_nodes[256];
	char fname_ways[256];
	char fname_relations[256];
	char fname_filter[256];
	snprintf(path_index, 256, "%s-index", argv[2]);
	snprintf(fname_nodes, 256, "%s-nodes.xml.gz", argv[2]);
	snprintf(fname_ways, 256, "%s-ways.xml.gz", argv[2]);
	snprintf(fname_relations, 256, "%s-relations.xml.gz", argv[2]);
	snprintf(fname_filter, 256, "%s", argv[1]);

	osmdb_filter_t* filter = osmdb_filter_new(fname_filter);
	if(filter == NULL)
	{
		return EXIT_FAILURE;
	}

	osmdb_index_t* index = osmdb_index_new(path_index);
	if(index == NULL)
	{
		goto fail_index;
	}

	osmdb_indexer_t indexer =
	{
		.index  = index,
		.filter = filter
	};

	// initialize chunk paths
	char path_node[256];
	char path_way[256];
	char path_relation[256];
	char path_noderef[256];
	char path_wayref[256];
	char path_ctrnode[256];
	char path_ctrway[256];
	char path_ctrrelation[256];
	char path_ctrnoderef[256];
	char path_ctrwayref[256];
	char path_ctrrelationref[256];
	osmdb_chunk_path(path_index, OSMDB_TYPE_NODE, path_node);
	osmdb_chunk_path(path_index, OSMDB_TYPE_WAY, path_way);
	osmdb_chunk_path(path_index, OSMDB_TYPE_RELATION, path_relation);
	osmdb_chunk_path(path_index, OSMDB_TYPE_NODEREF, path_noderef);
	osmdb_chunk_path(path_index, OSMDB_TYPE_WAYREF, path_wayref);
	osmdb_chunk_path(path_index, OSMDB_TYPE_CTRNODE, path_ctrnode);
	osmdb_chunk_path(path_index, OSMDB_TYPE_CTRWAY, path_ctrway);
	osmdb_chunk_path(path_index, OSMDB_TYPE_CTRRELATION, path_ctrrelation);
	osmdb_chunk_path(path_index, OSMDB_TYPE_CTRNODEREF, path_ctrnoderef);
	osmdb_chunk_path(path_index, OSMDB_TYPE_CTRWAYREF, path_ctrwayref);
	osmdb_chunk_path(path_index, OSMDB_TYPE_CTRRELATIONREF, path_ctrrelationref);
	if((osmdb_mkdir(path_node) == 0)        ||
	   (osmdb_mkdir(path_way) == 0)         ||
	   (osmdb_mkdir(path_relation) == 0)    ||
	   (osmdb_mkdir(path_noderef) == 0)     ||
	   (osmdb_mkdir(path_wayref) == 0)      ||
	   (osmdb_mkdir(path_ctrnode) == 0)     ||
	   (osmdb_mkdir(path_ctrway) == 0)      ||
	   (osmdb_mkdir(path_ctrrelation) == 0) ||
	   (osmdb_mkdir(path_ctrnoderef) == 0)  ||
	   (osmdb_mkdir(path_ctrwayref) == 0)   ||
	   (osmdb_mkdir(path_ctrrelationref) == 0))
	{
		goto fail_path;
	}

	LOGI("PARSE RELATIONS");
	if(osmdb_parse(fname_relations, (void*) &indexer,
	               nodeFn, wayFn, relationFn) == 0)
	{
		goto fail_parse;
	}

	LOGI("PARSE WAYS");
	if(osmdb_parse(fname_ways, (void*) &indexer,
	               nodeFn, wayFn, relationFn) == 0)
	{
		goto fail_parse;
	}

	LOGI("PARSE NODES");
	if(osmdb_parse(fname_nodes, (void*) &indexer,
	               nodeFn, wayFn, relationFn) == 0)
	{
		goto fail_parse;
	}

	LOGI("AMEND WAYS");
	if(osmdb_indexer_amendWays(&indexer) == 0)
	{
		goto fail_amend;
	}

	LOGI("AMEND RELATIONS");
	if(osmdb_indexer_amendRelations(&indexer) == 0)
	{
		goto fail_amend;
	}

	LOGI("FINISH INDEX");
	if(osmdb_index_delete(&index) == 0)
	{
		LOGE("FAILURE dt=%lf", a3d_timestamp() - t0);
		return EXIT_FAILURE;
	}

	osmdb_filter_delete(&filter);

	// success
	LOGE("SUCCESS dt=%lf", a3d_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_amend:
	fail_parse:
	fail_path:
		osmdb_index_delete(&index);
	fail_index:
		osmdb_filter_delete(&filter);
	LOGE("FAILURE dt=%lf", a3d_timestamp() - t0);
	return EXIT_FAILURE;
}

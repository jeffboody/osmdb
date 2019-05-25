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
#include <string.h>

#define LOG_TAG "osmdb"
#include "a3d/a3d_timestamp.h"
#include "libxmlstream/xml_log.h"
#include "osmdb/osmdb_parser.h"
#include "osmdb/osmdb_node.h"
#include "osmdb/osmdb_way.h"
#include "osmdb/osmdb_relation.h"
#include "osmdb/osmdb_util.h"
#include "osmdb/osmdb_index.h"
#include "osmdb/osmdb_chunk.h"
#include "osmdb/osmdb_filter.h"

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

static int isGreatLake(char* name)
{
	// the Great Lakes are very challenging to render
	// with raw OpenStreetMap data so we discard instead
	if(name &&
	   ((strcmp(name, "Lake Huron")    == 0) ||
	    (strcmp(name, "Lake Superior") == 0) ||
	    (strcmp(name, "Lake Michigan") == 0) ||
	    (strcmp(name, "Lake Erie")     == 0) ||
	    (strcmp(name, "Lake Ontario")  == 0)))
	{
		return 1;
	}

	return 0;
}

static int nodeErrFn(void* priv, osmdb_node_t* node)
{
	LOGE("invalid");
	return 0;
}

static int wayErrFn(void* priv, osmdb_way_t* way)
{
	LOGE("invalid");
	return 0;
}

static int relationErrFn(void* priv, osmdb_relation_t* relation)
{
	LOGE("invalid");
	return 0;
}

static int relationRefFn(void* priv, osmdb_relation_t* relation)
{
	assert(priv);
	assert(relation);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;
	if(osmdb_index_error(indexer->index))
	{
		LOGE("error");
		return 0;
	}

	stats_relations += 1.0;
	if(fmod(stats_relations, 100000.0) == 0.0)
	{
		LOGI("[R] %0.0lf", stats_relations);
		osmdb_index_stats(indexer->index);
	}

	if(isGreatLake(relation->name))
	{
		osmdb_relation_delete(&relation);
		return 1;
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
	}

	osmdb_relation_delete(&relation);
	return 1;
}

static int wayRefFn(void* priv, osmdb_way_t* way)
{
	assert(priv);
	assert(way);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;
	if(osmdb_index_error(indexer->index))
	{
		LOGE("error");
		return 0;
	}

	stats_ways += 1.0;
	if(fmod(stats_ways, 100000.0) == 0.0)
	{
		LOGI("[W] %0.0lf", stats_ways);
		osmdb_index_stats(indexer->index);
	}

	if(isGreatLake(way->name))
	{
		osmdb_way_delete(&way);
		return 1;
	}

	osmdb_filterInfo_t* info;
	info = osmdb_filter_selectWay(indexer->filter, way);
	if((info && (info->center == 0)) ||
	   osmdb_index_find(indexer->index,
	                    OSMDB_TYPE_WAYREF, way->id))
	{
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
	}
	else if((info && (info->center)) ||
	        osmdb_index_find(indexer->index,
	                         OSMDB_TYPE_CTRWAYREF, way->id))
	{
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
	}

	osmdb_way_delete(&way);
	return 1;
}

static int nodeFn(void* priv, osmdb_node_t* node)
{
	assert(priv);
	assert(node);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;
	if(osmdb_index_error(indexer->index))
	{
		LOGE("error");
		return 0;
	}

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
		int zoom     = -1;
		int selected = 0;
		if(info)
		{
			zoom     = info->zoom;
			selected = 1;
		}

		return osmdb_index_addNode(indexer->index,
		                           zoom, 0, selected, node);
	}
	else if(osmdb_index_find(indexer->index,
	                         OSMDB_TYPE_CTRNODEREF,
	                         node->id))
	{
		return osmdb_index_addNode(indexer->index,
		                           -1, 1, 0, node);
	}

	osmdb_node_delete(&node);
	return 1;
}

static int wayFn(void* priv, osmdb_way_t* way)
{
	assert(priv);
	assert(way);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;
	if(osmdb_index_error(indexer->index))
	{
		LOGE("error");
		return 0;
	}

	stats_ways += 1.0;
	if(fmod(stats_ways, 100000.0) == 0.0)
	{
		LOGI("[W] %0.0lf", stats_ways);
		osmdb_index_stats(indexer->index);
	}

	if(isGreatLake(way->name))
	{
		osmdb_way_delete(&way);
		return 1;
	}

	int zoom     = -1;
	int selected = 0;
	osmdb_filterInfo_t* info;
	info = osmdb_filter_selectWay(indexer->filter, way);
	if(info)
	{
		zoom     = info->zoom;
		selected = 1;
	}

	if((info && (info->center == 0)) ||
	   osmdb_index_find(indexer->index,
	                    OSMDB_TYPE_WAYREF, way->id))
	{

		return osmdb_index_addWay(indexer->index, zoom,
		                          0, selected, way);
	}
	else if((info && (info->center)) ||
	        osmdb_index_find(indexer->index,
	                         OSMDB_TYPE_CTRWAYREF, way->id))
	{
		return osmdb_index_addWay(indexer->index, zoom,
		                          1, selected, way);
	}

	osmdb_way_delete(&way);
	return 1;
}

static int relationFn(void* priv, osmdb_relation_t* relation)
{
	assert(priv);
	assert(relation);

	osmdb_indexer_t* indexer = (osmdb_indexer_t*) priv;
	if(osmdb_index_error(indexer->index))
	{
		LOGE("error");
		return 0;
	}

	stats_relations += 1.0;
	if(fmod(stats_relations, 100000.0) == 0.0)
	{
		LOGI("[R] %0.0lf", stats_relations);
		osmdb_index_stats(indexer->index);
	}

	if(isGreatLake(relation->name))
	{
		osmdb_relation_delete(&relation);
		return 1;
	}

	osmdb_filterInfo_t* info;
	info = osmdb_filter_selectRelation(indexer->filter,
	                                   relation);
	if(info == NULL)
	{
		osmdb_relation_delete(&relation);
		return 1;
	}

	return osmdb_index_addRelation(indexer->index, info->zoom,
	                               info->center, relation);
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	double t0 = a3d_timestamp();

	if(argc < 3)
	{
		LOGE("%s filter.xml PREFIX [PREFIX1 ... PREFIXN]", argv[0]);
		return EXIT_FAILURE;
	}
	const char* fname_filter = argv[1];
	const char* prefix       = argv[2];

	osmdb_filter_t* filter = osmdb_filter_new(fname_filter);
	if(filter == NULL)
	{
		return EXIT_FAILURE;
	}

	osmdb_index_t* index = osmdb_index_new(prefix);
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
	char path_ctrnode[256];
	char path_noderef[256];
	char path_wayref[256];
	char path_ctrnoderef[256];
	char path_ctrwayref[256];
	osmdb_chunk_path(prefix, OSMDB_TYPE_NODE, path_node);
	osmdb_chunk_path(prefix, OSMDB_TYPE_WAY, path_way);
	osmdb_chunk_path(prefix, OSMDB_TYPE_RELATION, path_relation);
	osmdb_chunk_path(prefix, OSMDB_TYPE_CTRNODE, path_ctrnode);
	osmdb_chunk_path(prefix, OSMDB_TYPE_NODEREF, path_noderef);
	osmdb_chunk_path(prefix, OSMDB_TYPE_WAYREF, path_wayref);
	osmdb_chunk_path(prefix, OSMDB_TYPE_CTRNODEREF, path_ctrnoderef);
	osmdb_chunk_path(prefix, OSMDB_TYPE_CTRWAYREF, path_ctrwayref);
	if((osmdb_mkdir(path_node)       == 0) ||
	   (osmdb_mkdir(path_way)        == 0) ||
	   (osmdb_mkdir(path_relation)   == 0) ||
	   (osmdb_mkdir(path_ctrnode)    == 0) ||
	   (osmdb_mkdir(path_noderef)    == 0) ||
	   (osmdb_mkdir(path_wayref)     == 0) ||
	   (osmdb_mkdir(path_ctrnoderef) == 0) ||
	   (osmdb_mkdir(path_ctrwayref)  == 0))
	{
		goto fail_path;
	}

	LOGI("PARSE RELATION REFS");
	int  i;
	int  start = 2; // start of prefixes
	char fname[256];
	for(i = start; i < argc; ++i)
	{
		prefix = argv[i];
		snprintf(fname, 256, "%s-relations.xml.gz", prefix);
		if(osmdb_parse(fname, (void*) &indexer,
		               nodeErrFn, wayErrFn, relationRefFn) == 0)
		{
			goto fail_parse;
		}
	}

	LOGI("PARSE WAY REFS");
	for(i = start; i < argc; ++i)
	{
		prefix = argv[i];
		snprintf(fname, 256, "%s-ways.xml.gz", prefix);
		if(osmdb_parse(fname, (void*) &indexer,
		               nodeErrFn, wayRefFn, relationErrFn) == 0)
		{
			goto fail_parse;
		}
	}

	LOGI("PARSE NODES");
	for(i = start; i < argc; ++i)
	{
		prefix = argv[i];
		snprintf(fname, 256, "%s-nodes.xml.gz", prefix);
		if(osmdb_parse(fname, (void*) &indexer,
		               nodeFn, wayErrFn, relationErrFn) == 0)
		{
			goto fail_parse;
		}
	}

	LOGI("PARSE WAYS");
	for(i = start; i < argc; ++i)
	{
		prefix = argv[i];
		snprintf(fname, 256, "%s-ways.xml.gz", prefix);
		if(osmdb_parse(fname, (void*) &indexer,
		               nodeErrFn, wayFn, relationErrFn) == 0)
		{
			goto fail_parse;
		}
	}

	LOGI("PARSE RELATIONS");
	for(i = start; i < argc; ++i)
	{
		prefix = argv[i];
		snprintf(fname, 256, "%s-relations.xml.gz", prefix);
		if(osmdb_parse(fname, (void*) &indexer,
		               nodeErrFn, wayErrFn, relationFn) == 0)
		{
			goto fail_parse;
		}
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
	fail_parse:
	fail_path:
		osmdb_index_delete(&index);
	fail_index:
		osmdb_filter_delete(&filter);
	LOGE("FAILURE dt=%lf", a3d_timestamp() - t0);
	return EXIT_FAILURE;
}

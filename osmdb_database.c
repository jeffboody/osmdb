/*
 * Copyright (c) 2020 Jeff Boody
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

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "../libcc/cc_multimap.h"
#include "../libcc/cc_unit.h"
#include "../libcc/math/cc_vec3f.h"
#include "../terrain/terrain_util.h"
#include "osmdb_database.h"
#include "osmdb_node.h"
#include "osmdb_relation.h"
#include "osmdb_util.h"
#include "osmdb_way.h"

/***********************************************************
* private                                                  *
***********************************************************/

static int OSMDB_ONE = 1;

#define OSMDB_DATABASE_CACHESIZE 4000000000

#define OSMDB_QUADRANT_NONE   0
#define OSMDB_QUADRANT_TOP    1
#define OSMDB_QUADRANT_LEFT   2
#define OSMDB_QUADRANT_BOTTOM 3
#define OSMDB_QUADRANT_RIGHT  4

static double osmdb_dot(double* a, double* b)
{
	ASSERT(a);
	ASSERT(b);

	return a[0]*b[0] + a[1]*b[1];
}

static int osmdb_quadrant(double* pc, double* tlc, double* trc)
{
	ASSERT(pc);
	ASSERT(tlc);
	ASSERT(trc);

	double tl = osmdb_dot(tlc, pc);
	double tr = osmdb_dot(trc, pc);

	if((tl > 0.0f) && (tr > 0.0f))
	{
		return OSMDB_QUADRANT_TOP;
	}
	else if((tl > 0.0f) && (tr <= 0.0f))
	{
		return OSMDB_QUADRANT_LEFT;
	}
	else if((tl <= 0.0f) && (tr <= 0.0f))
	{
		return OSMDB_QUADRANT_BOTTOM;
	}
	return OSMDB_QUADRANT_RIGHT;
}

static void osmdb_normalize(double* p)
{
	ASSERT(p);

	double mag = sqrt(p[0]*p[0] + p[1]*p[1]);
	p[0] = p[0]/mag;
	p[1] = p[1]/mag;
}

static void
osmdb_database_cat(char* words, int* len, const char* word)
{
	ASSERT(words);
	ASSERT(word);

	int idx = 0;
	while(*len < 255)
	{
		if(word[idx] == '\0')
		{
			return;
		}

		words[*len] = word[idx];
		*len += 1;
		words[*len] = '\0';
		++idx;
	}
}

static int
osmdb_database_prepareSet(osmdb_database_t* self,
                          const char* sql,
                          sqlite3_stmt*** _stmt)
{
	ASSERT(self);
	ASSERT(sql);
	ASSERT(_stmt);

	// note that the calling function should log an error

	sqlite3_stmt** stmt;
	stmt = (sqlite3_stmt**)
	        CALLOC(self->nthreads, sizeof(sqlite3_stmt*));
	if(stmt == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	int i;
	for(i = 0; i < self->nthreads; ++i)
	{
		if(sqlite3_prepare_v2(self->db, sql, -1,
		                      &(stmt[i]),
		                      NULL) != SQLITE_OK)
		{
			goto fail_prepare;
		}
	}

	*_stmt = stmt;

	// succcess
	return 1;

	// failure
	fail_prepare:
	{
		int j;
		for(j = 0; j < i; ++j)
		{
			sqlite3_finalize(stmt[j]);
		}
		FREE(stmt);
	}
	return 0;
}

static void
osmdb_database_finalizeSet(osmdb_database_t* self,
                           sqlite3_stmt*** _stmt)
{
	ASSERT(self);
	ASSERT(_stmt);

	sqlite3_stmt** stmt = *_stmt;
	if(stmt)
	{
		int i;
		for(i = 0; i < self->nthreads; ++i)
		{
			sqlite3_finalize(stmt[i]);
		}
		FREE(stmt);
		*_stmt = NULL;
	}
}

static int
osmdb_database_spellfixWord(osmdb_database_t* self,
                            int tid, const char* word,
                            char* spellfix, int* len)
{
	ASSERT(self);
	ASSERT(word);
	ASSERT(spellfix);

	sqlite3_stmt* stmt = self->stmt_spellfix[tid];

	int bytes = strlen(word) + 1;
	if(sqlite3_bind_text(stmt, self->idx_spellfix_arg, word,
	                     bytes, SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	if(sqlite3_step(stmt) == SQLITE_ROW)
	{
		const unsigned char* v;
		v = sqlite3_column_text(stmt, 0);
		osmdb_database_cat(spellfix, len, (const char*) v);
	}
	else
	{
		osmdb_database_cat(spellfix, len, word);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_searchTblNodes(osmdb_database_t* self,
                              int tid,
                              const char* text,
                              xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	int           idx  = self->idx_search_nodes_arg;
	sqlite3_stmt* stmt = self->stmt_search_nodes[tid];

	int bytes = strlen(text) + 1;
	if(sqlite3_bind_text(stmt, idx, text, bytes,
	                     SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		int         class = sqlite3_column_int(stmt, 0);
		const char* name  = (const char*)
		                    sqlite3_column_text(stmt, 1);
		const char* abrev = (const char*)
		                    sqlite3_column_text(stmt, 2);
		int         ele   = sqlite3_column_int(stmt, 3);
		int         st    = sqlite3_column_int(stmt, 4);
		double      lat   = sqlite3_column_double(stmt, 5);
		double      lon   = sqlite3_column_double(stmt, 6);

		xml_ostream_begin(os, "node");
		xml_ostream_attr(os, "name",
		                 abrev[0] == '\0' ? name : abrev);
		if(st)
		{
			xml_ostream_attr(os, "state", osmdb_stCodeToAbrev(st));
		}
		xml_ostream_attr(os, "class",
		                 osmdb_classCodeToName(class));
		xml_ostream_attrf(os, "rank", "%i",
		                 osmdb_classCodeToRank(class));
		xml_ostream_attrf(os, "lat", "%lf", lat);
		xml_ostream_attrf(os, "lon", "%lf", lon);
		if(ele)
		{
			xml_ostream_attrf(os, "ele", "%i",  ele);
		}
		xml_ostream_end(os);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_searchTblWays(osmdb_database_t* self,
                             int tid,
                             const char* text,
                             xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	int           idx  = self->idx_search_ways_arg;
	sqlite3_stmt* stmt = self->stmt_search_ways[tid];

	int bytes = strlen(text) + 1;
	if(sqlite3_bind_text(stmt, idx, text, bytes,
	                     SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		int         class = sqlite3_column_int(stmt, 0);
		const char* name  = (const char*)
		                    sqlite3_column_text(stmt, 1);
		const char* abrev = (const char*)
		                    sqlite3_column_text(stmt, 2);
		double      latT  = sqlite3_column_double(stmt, 3);
		double      lonL  = sqlite3_column_double(stmt, 4);
		double      latB  = sqlite3_column_double(stmt, 5);
		double      lonR  = sqlite3_column_double(stmt, 6);
		double      lat   = latB + (latT - latB)/2.0;
		double      lon   = lonL + (lonR - lonL)/2.0;

		xml_ostream_begin(os, "node");
		xml_ostream_attr(os, "name",
		                 abrev[0] == '\0' ? name : abrev);

		xml_ostream_attr(os, "class",
		                 osmdb_classCodeToName(class));
		xml_ostream_attrf(os, "rank", "%i",
		                 osmdb_classCodeToRank(class));
		xml_ostream_attrf(os, "lat", "%lf", lat);
		xml_ostream_attrf(os, "lon", "%lf", lon);
		xml_ostream_end(os);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_searchTblRels(osmdb_database_t* self,
                             int tid,
                             const char* text,
                             xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	int           idx  = self->idx_search_rels_arg;
	sqlite3_stmt* stmt = self->stmt_search_rels[tid];

	int bytes = strlen(text) + 1;
	if(sqlite3_bind_text(stmt, idx, text, bytes,
	                     SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		int         class = sqlite3_column_int(stmt, 0);
		const char* name  = (const char*)
		                    sqlite3_column_text(stmt, 1);
		const char* abrev = (const char*)
		                    sqlite3_column_text(stmt, 2);
		double      latT  = sqlite3_column_double(stmt, 3);
		double      lonL  = sqlite3_column_double(stmt, 4);
		double      latB  = sqlite3_column_double(stmt, 5);
		double      lonR  = sqlite3_column_double(stmt, 6);
		double      lat   = latB + (latT - latB)/2.0;
		double      lon   = lonL + (lonR - lonL)/2.0;

		xml_ostream_begin(os, "node");
		xml_ostream_attr(os, "name",
		                 abrev[0] == '\0' ? name : abrev);

		xml_ostream_attr(os, "class",
		                 osmdb_classCodeToName(class));
		xml_ostream_attrf(os, "rank", "%i",
		                 osmdb_classCodeToRank(class));
		xml_ostream_attrf(os, "lat", "%lf", lat);
		xml_ostream_attrf(os, "lon", "%lf", lon);
		xml_ostream_end(os);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_getNode(osmdb_database_t* self, double nid,
                       osmdb_node_t** _node)
{
	ASSERT(self);
	ASSERT(_node);

	pthread_mutex_lock(&self->object_mutex);

	// check cache
	osmdb_node_t*  node;
	cc_mapIter_t   miterator;
	cc_listIter_t* iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->object_map, &miterator,
	                    "n%0.0lf", nid);
	if(iter)
	{
		node = (osmdb_node_t*)
		       cc_list_peekIter(iter);
		osmdb_node_incref(node);
		cc_list_moven(self->object_list, iter, NULL);
		*_node = node;
		pthread_mutex_unlock(&self->object_mutex);
		return 1;
	}

	int ret = 0;
	int idx = self->idx_select_node_nid;

	sqlite3_stmt* stmt = self->stmt_select_node;
	if(sqlite3_bind_double(stmt, idx, nid) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		goto fail_bind;
	}

	int step = sqlite3_step(stmt);
	if(step == SQLITE_DONE)
	{
		// node may not exist due to osmosis
		ret = 1;
		goto fail_step;
	}
	else if(step != SQLITE_ROW)
	{
		LOGE("invalid nid=%0.0lf, step=%i", nid, step);
		goto fail_step;
	}

	double      lat   = sqlite3_column_double(stmt, 0);
	double      lon   = sqlite3_column_double(stmt, 1);
	const char* name  = (const char*) sqlite3_column_text(stmt, 2);
	const char* abrev = (const char*) sqlite3_column_text(stmt, 3);
	int         ele   = sqlite3_column_int(stmt, 4);
	int         st    = sqlite3_column_int(stmt, 5);
	int         class = sqlite3_column_int(stmt, 6);

	node = osmdb_node_new(nid, lat, lon, name, abrev,
	                      ele, st, class);
	if(node == NULL)
	{
		goto fail_node;
	}

	// update cache
	iter = cc_list_append(self->object_list, NULL,
	                      (const void*) node);
	if(iter == NULL)
	{
		goto fail_list;
	}

	if(cc_map_addf(self->object_map, (const void*) iter,
	               "n%0.0lf", nid) == 0)
	{
		goto fail_map;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	*_node = node;
	osmdb_node_incref(node);
	pthread_mutex_unlock(&self->object_mutex);

	// succcess
	return 1;

	// failure
	fail_map:
		cc_list_remove(self->object_list, &iter);
	fail_list:
		osmdb_node_delete(&node);
	fail_node:
	fail_step:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	fail_bind:
		pthread_mutex_unlock(&self->object_mutex);
	return ret;
}

static int
osmdb_database_getWayNdsLocked(osmdb_database_t* self,
                               osmdb_way_t* way)
{
	ASSERT(self);
	ASSERT(way);

	int idx = self->idx_select_wnds_wid;

	sqlite3_stmt* stmt = self->stmt_select_wnds;
	if(sqlite3_bind_double(stmt, idx, way->base.id) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		double ref = sqlite3_column_double(stmt, 0);
		if(osmdb_way_newNd(way, ref) == 0)
		{
			goto fail_nd;
		}
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	// success
	return 1;

	// failure
	fail_nd:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	return 0;
}

static int
osmdb_database_getWayCopy(osmdb_database_t* self,
                          double wid, int as_member,
                          osmdb_way_t** _way)
{
	ASSERT(self);
	ASSERT(_way);

	int ret      = 0;
	int idx      = self->idx_select_way_wid;
	int iname    = self->idx_select_way_name;
	int iabrev   = self->idx_select_way_abrev;
	int iclass   = self->idx_select_way_class;
	int ilayer   = self->idx_select_way_layer;
	int ioneway  = self->idx_select_way_oneway;
	int ibridge  = self->idx_select_way_bridge;
	int itunnel  = self->idx_select_way_tunnel;
	int icutting = self->idx_select_way_cutting;
	int icenter  = self->idx_select_way_center;
	int ilatT    = self->idx_select_way_latT;
	int ilonL    = self->idx_select_way_lonL;
	int ilatB    = self->idx_select_way_latB;
	int ilonR    = self->idx_select_way_lonR;

	pthread_mutex_lock(&self->object_mutex);

	// check cache
	osmdb_way_t*   way;
	cc_mapIter_t   miterator;
	cc_listIter_t* iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->object_map, &miterator,
	                    "w%0.0lf", wid);
	if(iter)
	{
		way = (osmdb_way_t*)
		      cc_list_peekIter(iter);
		cc_list_moven(self->object_list, iter, NULL);
		*_way = osmdb_way_copy(way);
		pthread_mutex_unlock(&self->object_mutex);
		return *_way ? 1 : 0;
	}

	sqlite3_stmt* stmt = self->stmt_select_way;
	if(sqlite3_bind_double(stmt, idx, wid) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		goto fail_bind;
	}

	int step = sqlite3_step(stmt);
	if(step == SQLITE_DONE)
	{
		// node may not exist due to osmosis
		ret = 1;
		goto fail_step;
	}
	else if(step != SQLITE_ROW)
	{
		LOGE("invalid wid=%0.0lf, step=%i", wid, step);
		goto fail_step;
	}

	const char* name    = (const char*)
	                      sqlite3_column_text(stmt, iname);
	const char* abrev   = (const char*)
	                      sqlite3_column_text(stmt, iabrev);
	int         class   = sqlite3_column_int(stmt, iclass);
	int         layer   = sqlite3_column_int(stmt, ilayer);
	int         oneway  = sqlite3_column_int(stmt, ioneway);
	int         bridge  = sqlite3_column_int(stmt, ibridge);
	int         tunnel  = sqlite3_column_int(stmt, itunnel);
	int         cutting = sqlite3_column_int(stmt, icutting);
	int         center  = sqlite3_column_int(stmt, icenter);
	double      latT    = sqlite3_column_double(stmt, ilatT);
	double      lonL    = sqlite3_column_double(stmt, ilonL);
	double      latB    = sqlite3_column_double(stmt, ilatB);
	double      lonR    = sqlite3_column_double(stmt, ilonR);

	way = osmdb_way_new(wid, name, abrev, class, layer,
	                    oneway, bridge, tunnel, cutting,
	                    latT, lonL, latB, lonR);
	if(way == NULL)
	{
		goto fail_way;
	}

	// center ways which style defines as points
	// but do not center way members
	if(as_member || (center == 0))
	{
		if(osmdb_database_getWayNdsLocked(self, way) == 0)
		{
			goto fail_way_nds;
		}
	}

	// update cache
	iter = cc_list_append(self->object_list, NULL,
	                      (const void*) way);
	if(iter == NULL)
	{
		goto fail_list;
	}

	if(cc_map_addf(self->object_map, (const void*) iter,
	               "w%0.0lf", wid) == 0)
	{
		goto fail_map;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	*_way = osmdb_way_copy(way);
	pthread_mutex_unlock(&self->object_mutex);

	// succcess
	return *_way ? 1 : 0;

	// failure
	fail_map:
		cc_list_remove(self->object_list, &iter);
	fail_list:
		osmdb_way_delete(&way);
	fail_way_nds:
		osmdb_way_delete(&way);
	fail_way:
	fail_step:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	fail_bind:
		pthread_mutex_unlock(&self->object_mutex);
	return ret;
}

static void
osmdb_database_putNode(osmdb_database_t* self,
                       osmdb_node_t** _node)
{
	ASSERT(self);
	ASSERT(_node);

	osmdb_node_t* node = *_node;
	if(node)
	{
		pthread_mutex_lock(&self->object_mutex);
		osmdb_node_decref(node);
		*_node = NULL;
		pthread_mutex_unlock(&self->object_mutex);
	}
}

static int
osmdb_database_getMemberNodesLocked(osmdb_database_t* self,
                                    osmdb_relation_t* rel)
{
	ASSERT(self);
	ASSERT(rel);

	int idx = self->idx_select_mnodes_rid;

	sqlite3_stmt* stmt = self->stmt_select_mnodes;
	if(sqlite3_bind_double(stmt, idx, rel->base.id) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int type = osmdb_relationMemberTypeToCode("node");
	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		double ref  = sqlite3_column_double(stmt, 0);
		int    role = sqlite3_column_int(stmt, 1);
		if(osmdb_relation_newMember(rel, type, ref, role) == 0)
		{
			goto fail_member;
		}
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	// success
	return 1;

	// failure
	fail_member:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	return 0;
}

static int
osmdb_database_getMemberWaysLocked(osmdb_database_t* self,
                                   osmdb_relation_t* rel)
{
	ASSERT(self);
	ASSERT(rel);

	int idx = self->idx_select_mways_rid;

	sqlite3_stmt* stmt = self->stmt_select_mways;
	if(sqlite3_bind_double(stmt, idx, rel->base.id) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int type = osmdb_relationMemberTypeToCode("way");
	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		double ref  = sqlite3_column_double(stmt, 0);
		int    role = sqlite3_column_int(stmt, 1);
		if(osmdb_relation_newMember(rel, type, ref, role) == 0)
		{
			goto fail_member;
		}
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	// success
	return 1;

	// failure
	fail_member:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	return 0;
}

static int
osmdb_database_getRelation(osmdb_database_t* self,
                           double rid,
                           double latT, double lonL,
                           double latB, double lonR,
                           osmdb_relation_t** _rel)
{
	ASSERT(self);

	int ret = 0;
	int idx = self->idx_select_relation_rid;

	pthread_mutex_lock(&self->object_mutex);

	// check cache
	osmdb_relation_t* rel;
	cc_mapIter_t      miterator;
	cc_listIter_t*    iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->object_map, &miterator,
	                    "r%0.0lf", rid);
	if(iter)
	{
		rel = (osmdb_relation_t*)
		      cc_list_peekIter(iter);
		osmdb_relation_incref(rel);
		cc_list_moven(self->object_list, iter, NULL);
		*_rel = rel;
		pthread_mutex_unlock(&self->object_mutex);
		return 1;
	}

	// select relation
	sqlite3_stmt* stmt = self->stmt_select_relation;
	if(sqlite3_bind_double(stmt, idx, rid) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		goto fail_bind;
	}

	int step = sqlite3_step(stmt);
	if(step == SQLITE_DONE)
	{
		// relation may not exist due to osmosis
		ret = 1;
		goto fail_step;
	}
	else if(step != SQLITE_ROW)
	{
		LOGE("invalid rid=%0.0lf, step=%i", rid, step);
		goto fail_step;
	}

	const char* name    = (const char*) sqlite3_column_text(stmt, 0);
	const char* abrev   = (const char*) sqlite3_column_text(stmt, 1);
	int         class   = sqlite3_column_int(stmt, 2);
	int         center  = sqlite3_column_int(stmt, 3);
	int         polygon = sqlite3_column_int(stmt, 4);

	rel = osmdb_relation_new(rid, name, abrev, class,
	                         latT, lonL, latB, lonR);
	if(rel == NULL)
	{
		goto fail_relation;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	if(osmdb_database_getMemberNodesLocked(self, rel) == 0)
	{
		goto fail_member_nodes;
	}

	// center relations which style defines as points
	// center large polygon relations
	// large areas are defined to be 50% of the area covered
	// by a "typical" zoom 14 tile. e.g.
	// 14/3403/6198:
	// latT=40.078071, lonL=-105.227051,
	// latB=40.061257, lonR=-105.205078,
	// area=0.000369
	float area = (float) ((latT-latB)*(lonR-lonL));
	if((center == 0) &&
	   ((polygon == 0) ||
	    (polygon && (0.5f*area < 0.000369f))))
	{
		if(osmdb_database_getMemberWaysLocked(self, rel) == 0)
		{
			goto fail_member_ways;
		}
	}

	// update cache
	iter = cc_list_append(self->object_list, NULL,
	                      (const void*) rel);
	if(iter == NULL)
	{
		goto fail_list;
	}

	if(cc_map_addf(self->object_map, (const void*) iter,
	               "r%0.0lf", rid) == 0)
	{
		goto fail_map;
	}

	*_rel = rel;
	osmdb_relation_incref(rel);
	pthread_mutex_unlock(&self->object_mutex);

	// succcess
	return 1;

	// failure
	fail_map:
		cc_list_remove(self->object_list, &iter);
	fail_list:
	fail_member_ways:
	fail_member_nodes:
		osmdb_relation_delete(&rel);
	fail_relation:
	fail_step:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	fail_bind:
		pthread_mutex_unlock(&self->object_mutex);
	return ret;
}

static void
osmdb_database_putRelation(osmdb_database_t* self,
                           osmdb_relation_t** _rel)
{
	ASSERT(self);
	ASSERT(_rel);

	osmdb_relation_t* rel = *_rel;
	if(rel)
	{
		pthread_mutex_lock(&self->object_mutex);
		osmdb_relation_decref(rel);
		*_rel = NULL;
		pthread_mutex_unlock(&self->object_mutex);
	}
}

static int
osmdb_database_gatherNode(osmdb_database_t* self,
                          double nid,
                          cc_map_t* map_export,
                          xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	// check if node is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator,
	                "n%0.0lf", nid))
	{
		return 1;
	}

	osmdb_node_t* node = NULL;
	int ret = osmdb_database_getNode(self, nid, &node);
	if(node == NULL)
	{
		// node may not exist due to osmosis
		return ret;
	}

	// mark the node as found
	if(cc_map_addf(map_export, (const void*) &OSMDB_ONE,
	               "n%0.0lf", nid) == 0)
	{
		goto fail_mark;
	}

	if(osmdb_node_export(node, os) == 0)
	{
		goto fail_export;
	}

	osmdb_database_putNode(self, &node);

	// success
	return 1;

	// failure
	fail_export:
		// ignore
	fail_mark:
		osmdb_database_putNode(self, &node);
	return 0;
}

static int
osmdb_database_sampleWay(osmdb_database_t* self,
                         int zoom, osmdb_way_t* way)
{
	ASSERT(self);
	ASSERT(way);

	float min_dist;
	if(zoom >= 14)
	{
		min_dist = self->min_dist14;
	}
	else if(zoom <= 8)
	{
		min_dist = self->min_dist8;
	}
	else
	{
		min_dist = self->min_dist11;
	}

	if(cc_list_size(way->nds) < 3)
	{
		return 1;
	}

	int            first = 1;
	float          onemi = cc_mi2m(5280.0f);
	cc_vec3f_t     p0    = { .x=0.0f, .y=0.0f, .z=0.0f };
	cc_listIter_t* iter  = cc_list_head(way->nds);
	osmdb_node_t*  node  = NULL;
	while(iter)
	{
		double* ref = (double*) cc_list_peekIter(iter);

		osmdb_database_getNode(self, *ref, &node);
		if(node == NULL)
		{
			iter = cc_list_next(iter);
			continue;
		}

		// accept the last nd
		cc_listIter_t* next = cc_list_next(iter);
		if(next == NULL)
		{
			osmdb_database_putNode(self, &node);
			return 1;
		}

		// compute distance between points
		cc_vec3f_t p1;
		terrain_geo2xyz(node->lat, node->lon, onemi,
		                &p1.x, &p1.y, &p1.z);
		float dist = cc_vec3f_distance(&p1, &p0);

		// check if the nd should be kept or discarded
		if(first || (dist >= min_dist))
		{
			cc_vec3f_copy(&p1, &p0);
			iter = cc_list_next(iter);
		}
		else
		{
			double* ref;
			ref = (double*)
			      cc_list_remove(way->nds, &iter);
			FREE(ref);
		}

		first = 0;
		osmdb_database_putNode(self, &node);
	}

	return 1;
}

static int
osmdb_database_gatherMemberWay(osmdb_database_t* self,
                               double wid, int zoom,
                               cc_map_t* map_export,
                               xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	// check if way is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator,
	                "w%0.0lf", wid))
	{
		return 1;
	}

	osmdb_way_t* way = NULL;
	int ret = osmdb_database_getWayCopy(self, wid, 1, &way);
	if(way == NULL)
	{
		// way may not exist due to osmosis
		return ret;
	}

	if(osmdb_database_sampleWay(self, zoom, way) == 0)
	{
		goto fail_sample;
	}

	// gather nodes
	cc_listIter_t* iter;
	iter = cc_list_head(way->nds);
	while(iter)
	{
		double* _ref = (double*) cc_list_peekIter(iter);
		if(osmdb_database_gatherNode(self, *_ref,
		                             map_export, os) == 0)
		{
			goto fail_nd;
		}

		iter = cc_list_next(iter);
	}

	// mark the way as found
	if(cc_map_addf(map_export, (const void*) &OSMDB_ONE,
	               "w%0.0lf", wid) == 0)
	{
		goto fail_mark;
	}

	if(osmdb_way_export(way, os) == 0)
	{
		goto fail_export;
	}

	osmdb_way_delete(&way);

	// success
	return 1;

	// failure
	fail_export:
		// ignore
	fail_mark:
	fail_nd:
	fail_sample:
		osmdb_way_delete(&way);
	return 0;
}

static int
osmdb_database_gatherRelation(osmdb_database_t* self,
                              double rid, int zoom,
                              double latT, double lonL,
                              double latB, double lonR,
                              cc_map_t* map_export,
                              xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	// check if relation is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator,
	                "r%0.0lf", rid))
	{
		return 1;
	}

	osmdb_relation_t* rel = NULL;
	int ret = osmdb_database_getRelation(self, rid, latT, lonL,
	                                     latB, lonR, &rel);
	if(rel == NULL)
	{
		// relation may not exist due to osmosis
		return ret;
	}

	int type_node = osmdb_relationMemberTypeToCode("node");
	int type_way  = osmdb_relationMemberTypeToCode("way");

	cc_listIter_t* iter;
	iter = cc_list_head(rel->members);
	while(iter)
	{
		osmdb_member_t* m;
		m = (osmdb_member_t*) cc_list_peekIter(iter);
		if(m->type == type_node)
		{
			if(osmdb_database_gatherNode(self, m->ref,
			                             map_export, os) == 0)
			{
				goto fail_member;
			}
		}
		else if(m->type == type_way)
		{
			if(osmdb_database_gatherMemberWay(self, m->ref, zoom,
			                                  map_export, os) == 0)
			{
				goto fail_member;
			}
		}

		iter = cc_list_next(iter);
	}

	// mark the relation as found
	if(cc_map_addf(map_export, (const void*) &OSMDB_ONE,
	               "r%0.0lf", rid) == 0)
	{
		goto fail_mark;
	}

	if(osmdb_relation_export(rel, os) == 0)
	{
		goto fail_export;
	}

	osmdb_database_putRelation(self, &rel);

	// success
	return 1;

	// failure
	fail_export:
		// ignore
	fail_mark:
	fail_member:
		osmdb_database_putRelation(self, &rel);
	return 0;
}

static int
osmdb_database_gatherNodes(osmdb_database_t* self,
                           int tid, int zoom,
                           double latT, double lonL,
                           double latB, double lonR,
                           cc_map_t* map_export,
                           xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	int idx_latT = self->idx_select_nodes_range_latT;
	int idx_lonL = self->idx_select_nodes_range_lonL;
	int idx_latB = self->idx_select_nodes_range_latB;
	int idx_lonR = self->idx_select_nodes_range_lonR;
	int idx_zoom = self->idx_select_nodes_range_zoom;

	sqlite3_stmt* stmt = self->stmt_select_nodes_range[tid];
	if((sqlite3_bind_double(stmt, idx_latT, latT) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonL, lonL) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_latB, latB) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonR, lonR) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_zoom, zoom) != SQLITE_OK))
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int ret = 1;
	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		double nid = sqlite3_column_double(stmt, 0);
		if(osmdb_database_gatherNode(self, nid,
		                             map_export, os) == 0)
		{
			ret = 0;
			break;
		}
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osmdb_database_gatherRelations(osmdb_database_t* self,
                               int tid, int zoom,
                               double latT, double lonL,
                               double latB, double lonR,
                               cc_map_t* map_export,
                               xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	int idx_latT  = self->idx_select_rels_range_latT;
	int idx_lonL  = self->idx_select_rels_range_lonL;
	int idx_latB  = self->idx_select_rels_range_latB;
	int idx_lonR  = self->idx_select_rels_range_lonR;
	int idx_zoom  = self->idx_select_rels_range_zoom;

	sqlite3_stmt* stmt = self->stmt_select_rels_range[tid];

	if((sqlite3_bind_double(stmt, idx_latT, latT) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonL, lonL) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_latB, latB) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonR, lonR) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_zoom, zoom) != SQLITE_OK))
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int ret = 1;
	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		double rid   = sqlite3_column_double(stmt, 0);
		double rlatT = sqlite3_column_double(stmt, 1);
		double rlonL = sqlite3_column_double(stmt, 2);
		double rlatB = sqlite3_column_double(stmt, 3);
		double rlonR = sqlite3_column_double(stmt, 4);
		if(osmdb_database_gatherRelation(self, rid, zoom,
		                                 rlatT, rlonL,
		                                 rlatB, rlonR,
		                                 map_export, os) == 0)
		{
			ret = 0;
			break;
		}
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osmdb_database_joinWay(osmdb_database_t* self,
                       osmdb_way_t* a, osmdb_way_t* b,
                       double ref1, double* ref2)
{
	ASSERT(self);
	ASSERT(a);
	ASSERT(b);
	ASSERT(ref2);

	// don't join a way with itself
	if(a == b)
	{
		return 0;
	}

	// check if way is complete
	double* refa1 = (double*) cc_list_peekHead(a->nds);
	double* refa2 = (double*) cc_list_peekTail(a->nds);
	double* refb1 = (double*) cc_list_peekHead(b->nds);
	double* refb2 = (double*) cc_list_peekTail(b->nds);
	if((refa1 == NULL) || (refa2 == NULL) ||
	   (refb1 == NULL) || (refb2 == NULL))
	{
		return 0;
	}

	// only try to join ways with multiple nds
	if((cc_list_size(a->nds) < 2) ||
	   (cc_list_size(b->nds) < 2))
	{
		return 0;
	}

	// don't try to join loops
	if((*refa1 == *refa2) || (*refb1 == *refb2))
	{
		return 0;
	}

	// check if ref1 is included in both ways and that
	// they can be joined head to tail
	int append;
	double* refp;
	double* refn;
	cc_listIter_t* next;
	cc_listIter_t* prev;
	if((ref1 == *refa1) && (ref1 == *refb2))
	{
		append = 0;
		*ref2  = *refb1;

		prev = cc_list_next(cc_list_head(a->nds));
		next = cc_list_prev(cc_list_tail(b->nds));
		refp = (double*) cc_list_peekIter(prev);
		refn = (double*) cc_list_peekIter(next);
	}
	else if((ref1 == *refa2) && (ref1 == *refb1))
	{
		append = 1;
		*ref2  = *refb2;

		prev = cc_list_prev(cc_list_tail(a->nds));
		next = cc_list_next(cc_list_head(b->nds));
		refp = (double*) cc_list_peekIter(prev);
		refn = (double*) cc_list_peekIter(next);
	}
	else
	{
		return 0;
	}

	// identify the nodes to be joined
	osmdb_node_t* node0 = NULL;
	osmdb_node_t* node1 = NULL;
	osmdb_node_t* node2 = NULL;
	osmdb_database_getNode(self, *refp, &node0);
	osmdb_database_getNode(self, ref1, &node1);
	osmdb_database_getNode(self, *refn, &node2);
	if((node0 == NULL) || (node1 == NULL) || (node2 == NULL))
	{
		osmdb_database_putNode(self, &node0);
		osmdb_database_putNode(self, &node1);
		osmdb_database_putNode(self, &node2);
		return 0;
	}

	// check join angle to prevent joining ways
	// at a sharp angle since this causes weird
	// rendering artifacts
	cc_vec3f_t p0;
	cc_vec3f_t p1;
	cc_vec3f_t p2;
	cc_vec3f_t v01;
	cc_vec3f_t v12;
	float onemi = cc_mi2m(5280.0f);
	terrain_geo2xyz(node0->lat, node0->lon, onemi,
	                &p0.x,  &p0.y, &p0.z);
	terrain_geo2xyz(node1->lat, node1->lon, onemi,
	                &p1.x,  &p1.y, &p1.z);
	terrain_geo2xyz(node2->lat, node2->lon, onemi,
	                &p2.x,  &p2.y, &p2.z);
	osmdb_database_putNode(self, &node0);
	osmdb_database_putNode(self, &node1);
	osmdb_database_putNode(self, &node2);
	cc_vec3f_subv_copy(&p1, &p0, &v01);
	cc_vec3f_subv_copy(&p2, &p1, &v12);
	cc_vec3f_normalize(&v01);
	cc_vec3f_normalize(&v12);
	float dot = cc_vec3f_dot(&v01, &v12);
	if(dot < cosf(cc_deg2rad(30.0f)))
	{
		return 0;
	}

	// check way attributes
	if((a->class   != b->class)  ||
	   (a->layer   != b->layer)  ||
	   (a->oneway  != b->oneway) ||
	   (a->bridge  != b->bridge) ||
	   (a->tunnel  != b->tunnel) ||
	   (a->cutting != b->cutting))
	{
		return 0;
	}

	// check name
	if(a->name && b->name)
	{
		if(strcmp(a->name, b->name) != 0)
		{
			return 0;
		}
	}
	else if(a->name || b->name)
	{
		return 0;
	}

	// join ways
	cc_listIter_t* iter;
	cc_listIter_t* temp;
	if(append)
	{
		// skip the first node
		iter = cc_list_head(b->nds);
		iter = cc_list_next(iter);
		while(iter)
		{
			temp = cc_list_next(iter);
			cc_list_swapn(b->nds, a->nds, iter, NULL);
			iter = temp;
		}
	}
	else
	{
		// skip the last node
		iter = cc_list_tail(b->nds);
		iter = cc_list_prev(iter);
		while(iter)
		{
			temp = cc_list_prev(iter);
			cc_list_swap(b->nds, a->nds, iter, NULL);
			iter = temp;
		}
	}

	// combine lat/lon
	if(b->latT > a->latT)
	{
		a->latT = b->latT;
	}
	if(b->lonL < a->lonL)
	{
		a->lonL = b->lonL;
	}
	if(b->latB < a->latB)
	{
		a->latB = b->latB;
	}
	if(b->lonR > a->lonR)
	{
		a->lonR = b->lonR;
	}

	return 1;
}

static int
osmdb_database_joinWays(osmdb_database_t* self,
                        cc_map_t* map_ways,
                        cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(map_ways);
	ASSERT(mm_nds_join);

	osmdb_way_t*       way1;
	osmdb_way_t*       way2;
	cc_multimapIter_t  miterator1;
	cc_multimapIter_t* miter1;
	cc_multimapIter_t  miterator2;
	cc_mapIter_t       hiter1;
	cc_mapIter_t       hiterator2;
	cc_mapIter_t*      hiter2;
	cc_listIter_t*     iter1;
	cc_listIter_t*     iter2;
	cc_list_t*         list1;
	cc_list_t*         list2;
	double*            id1;
	double*            id2;
	double             ref1;
	double             ref2;
	miter1 = cc_multimap_head(mm_nds_join, &miterator1);
	while(miter1)
	{
		ref1  = strtod(cc_multimap_key(miter1), NULL);
		list1 = (cc_list_t*) cc_multimap_list(miter1);
		iter1 = cc_list_head(list1);
		while(iter1)
		{
			id1  = (double*) cc_list_peekIter(iter1);
			if(*id1 == -1.0)
			{
				iter1 = cc_list_next(iter1);
				continue;
			}

			way1 = (osmdb_way_t*)
			       cc_map_findf(map_ways, &hiter1, "%0.0lf",
			                    *id1);
			if(way1 == NULL)
			{
				iter1 = cc_list_next(iter1);
				continue;
			}

			iter2 = cc_list_next(iter1);
			while(iter2)
			{
				hiter2 = &hiterator2;
				id2 = (double*) cc_list_peekIter(iter2);
				if(*id2 == -1.0)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				way2 = (osmdb_way_t*)
				       cc_map_findf(map_ways, hiter2, "%0.0lf",
				                    *id2);
				if(way2 == NULL)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				if(osmdb_database_joinWay(self, way1, way2,
				                          ref1, &ref2) == 0)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				// replace ref2->id2 with ref2->id1 in
				// mm_nds_join
				list2 = (cc_list_t*)
				        cc_multimap_findf(mm_nds_join, &miterator2,
				                          "%0.0lf", ref2);
				iter2 = cc_list_head(list2);
				while(iter2)
				{
					double* idx = (double*) cc_list_peekIter(iter2);
					if(*idx == *id2)
					{
						*idx = *id1;
						break;
					}

					iter2 = cc_list_next(iter2);
				}

				// remove ways from mm_nds_join
				*id1 = -1.0;
				*id2 = -1.0;

				// remove way2 from map_ways
				cc_map_remove(map_ways, &hiter2);
				osmdb_way_delete(&way2);
				iter2 = NULL;
			}

			iter1 = cc_list_next(iter1);
		}

		miter1 = cc_multimap_nextList(miter1);
	}

	return 1;
}

static int
osmdb_database_sampleWays(osmdb_database_t* self, int zoom,
                          cc_map_t* map_ways)
{
	ASSERT(self);
	ASSERT(map_ways);

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(map_ways, &miterator);
	while(miter)
	{
		osmdb_way_t* way = (osmdb_way_t*) cc_map_val(miter);
		if(osmdb_database_sampleWay(self, zoom, way) == 0)
		{
			return 0;
		}

		miter = cc_map_next(miter);
	}

	return 1;
}

static void
osmdb_database_clipWay(osmdb_database_t* self,
                       osmdb_way_t* way,
                       double latT, double lonL,
                       double latB, double lonR)
{
	ASSERT(self);
	ASSERT(way);

	// don't clip short ways
	if(cc_list_size(way->nds) <= 2)
	{
		return;
	}

	// check if way forms a loop
	double* first = (double*) cc_list_peekHead(way->nds);
	double* last  = (double*) cc_list_peekTail(way->nds);
	int     loop  = 0;
	if(*first == *last)
	{
		loop = 1;
	}

	/*
	 * quadrant setup
	 * remove (B), (E), (F), (L)
	 * remove A as well if not loop
	 *  \                          /
	 *   \        (L)             /
	 *    \      M        K      /
	 *  A  +--------------------+
	 *     |TLC        J     TRC|
	 *     |     N              | I
	 *     |                    |
	 * (B) |                    |
	 *     |         *          |
	 *     |         CENTER     |
	 *     |                    | H
	 *     |                    |
	 *   C +--------------------+
	 *    /                G     \
	 *   /  D          (F)        \
	 *  /         (E)              \
	 */
	int q0 = OSMDB_QUADRANT_NONE;
	int q1 = OSMDB_QUADRANT_NONE;
	int q2 = OSMDB_QUADRANT_NONE;
	double dlat = (latT - latB)/2.0;
	double dlon = (lonR - lonL)/2.0;
	double center[2] =
	{
		lonL + dlon,
		latB + dlat
	};
	double tlc[2] =
	{
		(lonL - center[0])/dlon,
		(latT - center[1])/dlat
	};
	double trc[2] =
	{
		(lonR - center[0])/dlon,
		(latT - center[1])/dlat
	};
	osmdb_normalize(tlc);
	osmdb_normalize(trc);

	// clip way
	double*         ref;
	osmdb_node_t*   node = NULL;
	cc_listIter_t* iter;
	cc_listIter_t* prev = NULL;
	iter = cc_list_head(way->nds);
	while(iter)
	{
		ref = (double*) cc_list_peekIter(iter);

		osmdb_database_getNode(self, *ref, &node);
		if(node == NULL)
		{
			// ignore
			iter = cc_list_next(iter);
			continue;
		}

		// check if node is clipped
		if((node->lat < latB) ||
		   (node->lat > latT) ||
		   (node->lon > lonR) ||
		   (node->lon < lonL))
		{
			// proceed to clipping
		}
		else
		{
			// not clipped by tile
			q0   = OSMDB_QUADRANT_NONE;
			q1   = OSMDB_QUADRANT_NONE;
			prev = NULL;
			iter = cc_list_next(iter);
			osmdb_database_putNode(self, &node);
			continue;
		}

		// compute the quadrant
		double pc[2] =
		{
			(node->lon - center[0])/dlon,
			(node->lat - center[1])/dlat
		};
		osmdb_normalize(pc);
		q2 = osmdb_quadrant(pc, tlc, trc);

		// mark the first and last node
		int clip_last = 0;
		if(iter == cc_list_head(way->nds))
		{
			if(loop)
			{
				q0 = OSMDB_QUADRANT_NONE;
				q1 = OSMDB_QUADRANT_NONE;
			}
			else
			{
				q0 = q2;
				q1 = q2;
			}
			prev = iter;
			iter = cc_list_next(iter);
			osmdb_database_putNode(self, &node);
			continue;
		}
		else if(iter == cc_list_tail(way->nds))
		{
			if((loop == 0) && (q1 == q2))
			{
				clip_last = 1;
			}
			else
			{
				// don't clip the prev node when
				// keeping the last node
				prev = NULL;
			}
		}

		// clip prev node
		if(prev && (q0 == q2) && (q1 == q2))
		{
			ref = (double*) cc_list_remove(way->nds, &prev);
			FREE(ref);
		}

		// clip last node
		if(clip_last)
		{
			ref = (double*) cc_list_remove(way->nds, &iter);
			FREE(ref);
			osmdb_database_putNode(self, &node);
			return;
		}

		q0   = q1;
		q1   = q2;
		prev = iter;
		iter = cc_list_next(iter);
		osmdb_database_putNode(self, &node);
	}
}

static int
osmdb_database_clipWays(osmdb_database_t* self,
                        double latT, double lonL,
                        double latB, double lonR,
                        cc_map_t* map_ways)
{
	ASSERT(self);
	ASSERT(map_ways);

	// elements are defined with zero width but in
	// practice are drawn with non-zero width
	// points/lines so an offset is needed to ensure they
	// are not clipped between neighboring tiles
	double dlat = (latT - latB)/16.0;
	double dlon = (lonR - lonL)/16.0;
	latT += dlat;
	latB -= dlat;
	lonL -= dlon;
	lonR += dlon;

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(map_ways, &miterator);
	while(miter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*) cc_map_val(miter);

		osmdb_database_clipWay(self, way,
		                       latT, lonL, latB, lonR);

		miter = cc_map_next(miter);
	}

	return 1;
}

static int
osmdb_database_exportWays(osmdb_database_t* self,
                          xml_ostream_t* os,
                          cc_map_t* map_ways,
                          cc_map_t* map_export)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_ways);
	ASSERT(map_export);

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(map_ways, &miterator);
	while(miter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*) cc_map_val(miter);

		// gather nds
		cc_listIter_t* iter = cc_list_head(way->nds);
		while(iter)
		{
			double* ref = (double*) cc_list_peekIter(iter);
			if(osmdb_database_gatherNode(self, *ref,
			                             map_export, os) == 0)
			{
				return 0;
			}
			iter = cc_list_next(iter);
		}

		if(osmdb_way_export(way, os) == 0)
		{
			return 0;
		}

		miter = cc_map_next(miter);
	}

	return 1;
}

static int
osmdb_database_gatherWay(osmdb_database_t* self,
                         double wid,
                         cc_map_t* map_export,
                         cc_map_t* map_ways,
                         cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(map_ways);
	ASSERT(mm_nds_join);

	// check if way is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator,
	                "w%0.0lf", wid))
	{
		return 1;
	}

	osmdb_way_t* way = NULL;
	int ret = osmdb_database_getWayCopy(self, wid, 0, &way);
	if(way == NULL)
	{
		// way may not exist due to osmosis
		return ret;
	}

	if(cc_map_addf(map_ways, (const void*) way, "%0.0lf",
	               wid) == 0)
	{
		osmdb_way_delete(&way);
		return 0;
	}

	// check if way is complete
	double* ref1 = (double*) cc_list_peekHead(way->nds);
	double* ref2 = (double*) cc_list_peekTail(way->nds);
	if((ref1 == NULL) || (ref2 == NULL))
	{
		return 1;
	}

	// otherwise add join nds
	double* id1_copy = (double*)
	                   MALLOC(sizeof(double));
	if(id1_copy == NULL)
	{
		return 0;
	}
	*id1_copy = wid;

	double* id2_copy = (double*)
	                   MALLOC(sizeof(double));
	if(id2_copy == NULL)
	{
		FREE(id1_copy);
		return 0;
	}
	*id2_copy = wid;

	if(cc_multimap_addf(mm_nds_join, (const void*) id1_copy,
	                    "%0.0lf", *ref1) == 0)
	{
		FREE(id1_copy);
		FREE(id2_copy);
		return 0;
	}

	if(cc_multimap_addf(mm_nds_join, (const void*) id2_copy,
	                    "%0.0lf", *ref2) == 0)
	{
		FREE(id2_copy);
		return 0;
	}

	return 1;
}

static int
osmdb_database_gatherWays(osmdb_database_t* self,
                          int tid, int zoom,
                          double latT, double lonL,
                          double latB, double lonR,
                          cc_map_t* map_export,
                          xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	int ret      = 1;
	int idx_latT = self->idx_select_ways_range_latT;
	int idx_lonL = self->idx_select_ways_range_lonL;
	int idx_latB = self->idx_select_ways_range_latB;
	int idx_lonR = self->idx_select_ways_range_lonR;
	int idx_zoom = self->idx_select_ways_range_zoom;

	cc_map_t* map_ways = cc_map_new();
	if(map_ways == NULL)
	{
		return 0;
	}

	cc_multimap_t* mm_nds_join = cc_multimap_new(NULL);
	if(mm_nds_join == NULL)
	{
		ret = 0;
		goto fail_mm_nds_join;
	}

	sqlite3_stmt* stmt = self->stmt_select_ways_range[tid];
	if((sqlite3_bind_double(stmt, idx_latT, latT) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonL, lonL) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_latB, latB) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonR, lonR) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_zoom, zoom) != SQLITE_OK))
	{
		LOGE("sqlite3_bind_double failed");
		ret = 0;
		goto fail_bind;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		double wid = sqlite3_column_double(stmt, 0);
		if(osmdb_database_gatherWay(self, wid,
		                            map_export, map_ways,
		                            mm_nds_join) == 0)
		{
			ret = 0;
			goto fail_gather_way;
		}
	}

	if(osmdb_database_joinWays(self, map_ways,
	                           mm_nds_join) == 0)
	{
		goto fail_join;
	}

	if(osmdb_database_sampleWays(self, zoom, map_ways) == 0)
	{
		goto fail_sample;
	}

	if(osmdb_database_clipWays(self,
	                           latT, lonL, latB, lonR,
	                           map_ways) == 0)
	{
		goto fail_clip;
	}

	if(osmdb_database_exportWays(self, os, map_ways,
	                             map_export) == 0)
	{
		goto fail_export;
	}

	// success or failure
	fail_export:
	fail_clip:
	fail_sample:
	fail_join:
	fail_gather_way:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	fail_bind:
	{
		cc_multimapIter_t  miterator;
		cc_multimapIter_t* miter;
		miter = cc_multimap_head(mm_nds_join, &miterator);
		while(miter)
		{
			double* ref;
			ref = (double*)
			      cc_multimap_remove(mm_nds_join, &miter);
			FREE(ref);
		}
		cc_multimap_delete(&mm_nds_join);
	}
	fail_mm_nds_join:
	{
		cc_mapIter_t  miterator;
		cc_mapIter_t* miter = cc_map_head(map_ways, &miterator);
		while(miter)
		{
			osmdb_way_t* way;
			way = (osmdb_way_t*)
			      cc_map_remove(map_ways, &miter);
			osmdb_way_delete(&way);
		}
		cc_map_delete(&map_ways);
	}
	return ret;
}

static void
osmdb_database_computeMinDist(osmdb_database_t* self)
{
	ASSERT(self);

	// compute tile at home location
	double home_lat = 40.061295;
	double home_lon = -105.214552;
	float tx_8;
	float ty_8;
	float tx_11;
	float ty_11;
	float tx_14;
	float ty_14;
	terrain_coord2tile(home_lat, home_lon, 8,
	                   &tx_8, &ty_8);
	terrain_coord2tile(home_lat, home_lon, 11,
	                   &tx_11, &ty_11);
	terrain_coord2tile(home_lat, home_lon, 14,
	                   &tx_14, &ty_14);
	float txa_8  = (float) ((int) tx_8);
	float tya_8  = (float) ((int) tx_8);
	float txa_11 = (float) ((int) tx_11);
	float tya_11 = (float) ((int) tx_11);
	float txa_14 = (float) ((int) tx_14);
	float tya_14 = (float) ((int) tx_14);
	float txb_8  = txa_8  + 1.0f;
	float tyb_8  = tya_8  + 1.0f;
	float txb_11 = txa_11 + 1.0f;
	float tyb_11 = tya_11 + 1.0f;
	float txb_14 = txa_14 + 1.0f;
	float tyb_14 = tya_14 + 1.0f;

	// compute coords at home tiles
	double latT_8;
	double lonL_8;
	double latB_8;
	double lonR_8;
	double latT_11;
	double lonL_11;
	double latB_11;
	double lonR_11;
	double latT_14;
	double lonL_14;
	double latB_14;
	double lonR_14;
	terrain_tile2coord(txa_8, tya_8, 8,
	                   &latT_8, &lonL_8);
	terrain_tile2coord(txb_8, tyb_8, 8,
	                   &latB_8, &lonR_8);
	terrain_tile2coord(txa_11, tya_11, 11,
	                   &latT_11, &lonL_11);
	terrain_tile2coord(txb_11, tyb_11, 11,
	                   &latB_11, &lonR_11);
	terrain_tile2coord(txa_14, tya_14, 14,
	                   &latT_14, &lonL_14);
	terrain_tile2coord(txb_14, tyb_14, 14,
	                   &latB_14, &lonR_14);

	// compute x,y at home tiles
	cc_vec3f_t pa_8;
	cc_vec3f_t pb_8;
	cc_vec3f_t pa_11;
	cc_vec3f_t pb_11;
	cc_vec3f_t pa_14;
	cc_vec3f_t pb_14;
	float onemi = cc_mi2m(5280.0f);
	terrain_geo2xyz(latT_8, lonL_8, onemi,
	                 &pa_8.x, &pa_8.y, &pa_8.z);
	terrain_geo2xyz(latB_8, lonR_8, onemi,
	                 &pb_8.x, &pb_8.y, &pb_8.z);
	terrain_geo2xyz(latT_11, lonL_11, onemi,
	                 &pa_11.x, &pa_11.y, &pa_11.z);
	terrain_geo2xyz(latB_11, lonR_11, onemi,
	                 &pb_11.x, &pb_11.y, &pb_11.z);
	terrain_geo2xyz(latT_14, lonL_14, onemi,
	                 &pa_14.x, &pa_14.y, &pa_14.z);
	terrain_geo2xyz(latB_14, lonR_14, onemi,
	                 &pb_14.x, &pb_14.y, &pb_14.z);

	// compute min_dist
	// scale by 1/8th since each tile serves 3 zoom levels
	float s   = 1.0f/8.0f;
	float pix = sqrtf(2*256.0f*256.0f);
	self->min_dist8  = s*cc_vec3f_distance(&pb_8, &pa_8)/pix;
	self->min_dist11 = s*cc_vec3f_distance(&pb_11, &pa_11)/pix;
	self->min_dist14 = s*cc_vec3f_distance(&pb_14, &pa_14)/pix;
}

static void
osmdb_database_trimCache(osmdb_database_t* self)
{
	ASSERT(self);

	pthread_mutex_lock(&self->object_mutex);

	cc_listIter_t* iter;
	iter = cc_list_head(self->object_list);
	while(iter)
	{
		if(MEMSIZE() <= OSMDB_DATABASE_CACHESIZE)
		{
			break;
		}

		osmdb_object_t* obj;
		obj = (osmdb_object_t*) cc_list_peekIter(iter);
		if(obj->refcount)
		{
			iter = cc_list_next(iter);
			continue;
		}

		cc_mapIter_t  miterator;
		cc_mapIter_t* miter = &miterator;
		if(obj->type == OSMDB_OBJECT_TYPE_NODE)
		{
			cc_map_findf(self->object_map, miter,
			             "n%0.0lf", obj->id);
			cc_map_remove(self->object_map, &miter);
			osmdb_node_delete((osmdb_node_t**) &obj);
		}
		else if(obj->type == OSMDB_OBJECT_TYPE_WAY)
		{
			cc_map_findf(self->object_map, miter,
			             "w%0.0lf", obj->id);
			cc_map_remove(self->object_map, &miter);
			osmdb_way_delete((osmdb_way_t**) &obj);
		}
		else if(obj->type == OSMDB_OBJECT_TYPE_RELATION)
		{
			cc_map_findf(self->object_map, miter,
			             "r%0.0lf", obj->id);
			cc_map_remove(self->object_map, &miter);
			osmdb_relation_delete((osmdb_relation_t**) &obj);
		}
		cc_list_remove(self->object_list, &iter);
	}

	pthread_mutex_unlock(&self->object_mutex);
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_database_t* osmdb_database_new(const char* fname,
                                     int nthreads)
{
	ASSERT(fname);

	osmdb_database_t* self;
	self = (osmdb_database_t*)
	       CALLOC(1, sizeof(osmdb_database_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->nthreads = nthreads;

	if(sqlite3_initialize() != SQLITE_OK)
	{
		LOGE("sqlite3_initialize failed");
		goto fail_init;
	}

	if(sqlite3_open_v2(fname, &self->db,
	                   SQLITE_OPEN_READONLY,
	                   NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 failed");
		goto fail_open;
	}

	sqlite3_enable_load_extension(self->db, 1);
	if(sqlite3_load_extension(self->db, "./spellfix.so",
	                          NULL, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_load_extension failed");
		goto fail_extension;
	}

	const char sql_spellfix[256] =
		"SELECT word FROM tbl_spellfix"
		"	WHERE word MATCH @arg AND top=5;";

	if(osmdb_database_prepareSet(self, sql_spellfix,
	                             &self->stmt_spellfix) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_spellfix;
	}

	const char sql_search_nodes[256] =
		"SELECT class, name, abrev, ele, st, lat, lon FROM tbl_nodes_text"
		"	JOIN tbl_nodes_info USING (nid)"
		"	JOIN tbl_nodes_coords USING (nid)"
		"	JOIN tbl_class_rank USING (class)"
		"	WHERE txt MATCH @arg"
		"	ORDER BY rank DESC"
		"	LIMIT 10;";

	if(osmdb_database_prepareSet(self, sql_search_nodes,
	                             &self->stmt_search_nodes) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_search_nodes;
	}

	const char sql_search_ways[256] =
		"SELECT class, name, abrev, latT, lonL, latB, lonR FROM tbl_ways_text"
		"	JOIN tbl_ways USING (wid)"
		"	JOIN tbl_ways_range USING (wid)"
		"	JOIN tbl_class_rank USING (class)"
		"	WHERE txt MATCH @arg"
		"	ORDER BY rank DESC"
		"	LIMIT 10;";

	if(osmdb_database_prepareSet(self, sql_search_ways,
	                             &self->stmt_search_ways) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_search_ways;
	}

	const char sql_search_rels[256] =
		"SELECT class, name, abrev, latT, lonL, latB, lonR FROM tbl_rels_text"
		"	JOIN tbl_rels USING (rid)"
		"	JOIN tbl_rels_range USING (rid)"
		"	JOIN tbl_class_rank USING (class)"
		"	WHERE txt MATCH @arg"
		"	ORDER BY rank DESC"
		"	LIMIT 10;";

	if(osmdb_database_prepareSet(self, sql_search_rels,
	                             &self->stmt_search_rels) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_search_rels;
	}

	const char sql_select_nodes_range[256] =
		"SELECT nid FROM tbl_nodes_range"
		"	JOIN tbl_nodes_info USING (nid)"
		"	WHERE latT>@arg_latB AND lonL<@arg_lonR AND"
		"	      latB<@arg_latT AND lonR>@arg_lonL AND"
		"	      min_zoom<=@arg_zoom;";

	if(osmdb_database_prepareSet(self, sql_select_nodes_range,
	                             &self->stmt_select_nodes_range) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_nodes_range;
	}

	const char sql_select_node[256] =
		"SELECT lat, lon, name, abrev, ele, st, class FROM tbl_nodes_coords"
		"	LEFT OUTER JOIN tbl_nodes_info USING (nid)"
		"	WHERE nid=@arg;";

	if(sqlite3_prepare_v2(self->db, sql_select_node, -1,
	                      &self->stmt_select_node,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_node;
	}

	const char sql_select_rels_range[256] =
		"SELECT rid, latT, lonL, latB, lonR FROM tbl_rels_range"
		"	JOIN tbl_rels USING (rid)"
		"	WHERE latT>@arg_latB AND lonL<@arg_lonR AND"
		"	      latB<@arg_latT AND lonR>@arg_lonL AND"
		"	      min_zoom<=@arg_zoom;";

	if(osmdb_database_prepareSet(self, sql_select_rels_range,
	                             &self->stmt_select_rels_range) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_rels_range;
	}

	const char sql_select_relation[256] =
		"SELECT name, abrev, class, center, polygon FROM tbl_rels"
		"	WHERE rid=@arg;";

	if(sqlite3_prepare_v2(self->db, sql_select_relation, -1,
	                      &self->stmt_select_relation,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_relation;
	}

	const char sql_select_mnodes[256] =
		"SELECT nid, role FROM tbl_nodes_members"
		"	WHERE rid=@arg;";

	if(sqlite3_prepare_v2(self->db, sql_select_mnodes, -1,
	                      &self->stmt_select_mnodes,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_mnodes;
	}

	const char sql_select_mways[256] =
		"SELECT wid, role FROM tbl_ways_members"
		"	WHERE rid=@arg"
		"	ORDER BY idx;";

	if(sqlite3_prepare_v2(self->db, sql_select_mways, -1,
	                      &self->stmt_select_mways,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_mways;
	}

	const char sql_select_way[256] =
		"SELECT * FROM tbl_ways"
		"	JOIN tbl_ways_range USING (wid)"
		"	WHERE wid=@arg;";

	if(sqlite3_prepare_v2(self->db, sql_select_way, -1,
	                      &self->stmt_select_way,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_way;
	}

	const char sql_select_wnds[256] =
		"SELECT nid FROM tbl_ways_nds"
		"	WHERE wid=@arg"
		"	ORDER BY idx;";

	if(sqlite3_prepare_v2(self->db, sql_select_wnds, -1,
	                      &self->stmt_select_wnds,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_wnds;
	}

	const char sql_select_ways_range[256] =
		"SELECT wid FROM tbl_ways_range"
		"	JOIN tbl_ways USING (wid)"
		"	WHERE latT>@arg_latB AND lonL<@arg_lonR AND"
		"	      latB<@arg_latT AND lonR>@arg_lonL AND"
		"	      min_zoom<=@arg_zoom AND selected=1;";

	if(osmdb_database_prepareSet(self, sql_select_ways_range,
	                             &self->stmt_select_ways_range) == 0)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_select_ways_range;
	}

	self->idx_spellfix_arg     = sqlite3_bind_parameter_index(self->stmt_spellfix[0], "@arg");
	self->idx_search_nodes_arg = sqlite3_bind_parameter_index(self->stmt_search_nodes[0], "@arg");
	self->idx_search_ways_arg  = sqlite3_bind_parameter_index(self->stmt_search_ways[0], "@arg");
	self->idx_search_rels_arg  = sqlite3_bind_parameter_index(self->stmt_search_rels[0], "@arg");

	self->idx_select_nodes_range_latT = sqlite3_bind_parameter_index(self->stmt_select_nodes_range[0], "@arg_latT");
	self->idx_select_nodes_range_lonL = sqlite3_bind_parameter_index(self->stmt_select_nodes_range[0], "@arg_lonL");
	self->idx_select_nodes_range_latB = sqlite3_bind_parameter_index(self->stmt_select_nodes_range[0], "@arg_latB");
	self->idx_select_nodes_range_lonR = sqlite3_bind_parameter_index(self->stmt_select_nodes_range[0], "@arg_lonR");
	self->idx_select_nodes_range_zoom = sqlite3_bind_parameter_index(self->stmt_select_nodes_range[0], "@arg_zoom");
	self->idx_select_node_nid         = sqlite3_bind_parameter_index(self->stmt_select_node, "@arg");
	self->idx_select_rels_range_latT  = sqlite3_bind_parameter_index(self->stmt_select_rels_range[0], "@arg_latT");
	self->idx_select_rels_range_lonL  = sqlite3_bind_parameter_index(self->stmt_select_rels_range[0], "@arg_lonL");
	self->idx_select_rels_range_latB  = sqlite3_bind_parameter_index(self->stmt_select_rels_range[0], "@arg_latB");
	self->idx_select_rels_range_lonR  = sqlite3_bind_parameter_index(self->stmt_select_rels_range[0], "@arg_lonR");
	self->idx_select_rels_range_zoom  = sqlite3_bind_parameter_index(self->stmt_select_rels_range[0], "@arg_zoom");
	self->idx_select_relation_rid     = sqlite3_bind_parameter_index(self->stmt_select_relation, "@arg");
	self->idx_select_mnodes_rid       = sqlite3_bind_parameter_index(self->stmt_select_mnodes, "@arg");
	self->idx_select_mways_rid        = sqlite3_bind_parameter_index(self->stmt_select_mways, "@arg");
	self->idx_select_way_wid          = sqlite3_bind_parameter_index(self->stmt_select_way, "@arg");
	self->idx_select_wnds_wid         = sqlite3_bind_parameter_index(self->stmt_select_wnds, "@arg");
	self->idx_select_ways_range_latT  = sqlite3_bind_parameter_index(self->stmt_select_ways_range[0], "@arg_latT");
	self->idx_select_ways_range_lonL  = sqlite3_bind_parameter_index(self->stmt_select_ways_range[0], "@arg_lonL");
	self->idx_select_ways_range_latB  = sqlite3_bind_parameter_index(self->stmt_select_ways_range[0], "@arg_latB");
	self->idx_select_ways_range_lonR  = sqlite3_bind_parameter_index(self->stmt_select_ways_range[0], "@arg_lonR");
	self->idx_select_ways_range_zoom  = sqlite3_bind_parameter_index(self->stmt_select_ways_range[0], "@arg_zoom");

	// sqlite only allows for the selection of up to 10 items
	// which is not enough to construct a way so we must
	// find the index of each individual item
	int i;
	int count = sqlite3_column_count(self->stmt_select_way);
	for(i = 0; i < count; ++i)
	{
		const char* col;
		col = sqlite3_column_name(self->stmt_select_way, i);

		if(strcmp(col, "name") == 0)
		{
			self->idx_select_way_name = i;
		}
		else if(strcmp(col, "abrev") == 0)
		{
			self->idx_select_way_abrev = i;
		}
		else if(strcmp(col, "class") == 0)
		{
			self->idx_select_way_class = i;
		}
		else if(strcmp(col, "layer") == 0)
		{
			self->idx_select_way_layer = i;
		}
		else if(strcmp(col, "oneway") == 0)
		{
			self->idx_select_way_oneway = i;
		}
		else if(strcmp(col, "bridge") == 0)
		{
			self->idx_select_way_bridge = i;
		}
		else if(strcmp(col, "tunnel") == 0)
		{
			self->idx_select_way_tunnel = i;
		}
		else if(strcmp(col, "cutting") == 0)
		{
			self->idx_select_way_cutting = i;
		}
		else if(strcmp(col, "center") == 0)
		{
			self->idx_select_way_center = i;
		}
		else if(strcmp(col, "latT") == 0)
		{
			self->idx_select_way_latT = i;
		}
		else if(strcmp(col, "lonL") == 0)
		{
			self->idx_select_way_lonL = i;
		}
		else if(strcmp(col, "latB") == 0)
		{
			self->idx_select_way_latB = i;
		}
		else if(strcmp(col, "lonR") == 0)
		{
			self->idx_select_way_lonR = i;
		}
	}

	osmdb_database_computeMinDist(self);

	self->object_list = cc_list_new();
	if(self->object_list == NULL)
	{
		goto fail_object_list;
	}

	self->object_map = cc_map_new();
	if(self->object_map == NULL)
	{
		goto fail_object_map;
	}

	if(pthread_mutex_init(&self->object_mutex, NULL) != 0)
	{
		LOGE("pthread_mutex_init failed");
		goto fail_object_mutex;
	}

	// success
	return self;

	// failure
	fail_object_mutex:
		cc_map_delete(&self->object_map);
	fail_object_map:
		cc_list_delete(&self->object_list);
	fail_object_list:
		osmdb_database_finalizeSet(self,
		                           &self->stmt_select_ways_range);
	fail_prepare_select_ways_range:
		sqlite3_finalize(self->stmt_select_wnds);
	fail_prepare_select_wnds:
		sqlite3_finalize(self->stmt_select_way);
	fail_prepare_select_way:
		sqlite3_finalize(self->stmt_select_mways);
	fail_prepare_select_mways:
		sqlite3_finalize(self->stmt_select_mnodes);
	fail_prepare_select_mnodes:
		sqlite3_finalize(self->stmt_select_relation);
	fail_prepare_select_relation:
		osmdb_database_finalizeSet(self,
		                           &self->stmt_select_rels_range);
	fail_prepare_select_rels_range:
		sqlite3_finalize(self->stmt_select_node);
	fail_prepare_select_node:
		osmdb_database_finalizeSet(self,
		                           &self->stmt_select_nodes_range);
	fail_prepare_select_nodes_range:
		osmdb_database_finalizeSet(self, &self->stmt_search_rels);
	fail_prepare_search_rels:
		osmdb_database_finalizeSet(self, &self->stmt_search_ways);
	fail_prepare_search_ways:
		osmdb_database_finalizeSet(self,
		                           &self->stmt_search_nodes);
	fail_prepare_search_nodes:
		osmdb_database_finalizeSet(self, &self->stmt_spellfix);
	fail_prepare_spellfix:
	fail_extension:
	fail_open:
	{
		// close db even when open fails
		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(sqlite3_shutdown() != SQLITE_OK)
		{
			LOGW("sqlite3_shutdown failed");
		}
	}
	fail_init:
		FREE(self);
	return NULL;
}

void osmdb_database_delete(osmdb_database_t** _self)
{
	ASSERT(_self);

	osmdb_database_t* self = *_self;
	if(self)
	{
		// empty list/map
		cc_mapIter_t    miterator;
		cc_mapIter_t*   miter;
		cc_listIter_t*  iter;
		osmdb_object_t* obj;
		miter = cc_map_head(self->object_map, &miterator);
		while(miter)
		{
			iter = (cc_listIter_t*)
			       cc_map_remove(self->object_map, &miter);
			obj  = (osmdb_object_t*)
			       cc_list_remove(self->object_list, &iter);

			if(obj->type == OSMDB_OBJECT_TYPE_NODE)
			{
				osmdb_node_delete((osmdb_node_t**) &obj);
			}
			else if(obj->type == OSMDB_OBJECT_TYPE_WAY)
			{
				osmdb_way_delete((osmdb_way_t**) &obj);
			}
			else if(obj->type == OSMDB_OBJECT_TYPE_RELATION)
			{
				osmdb_relation_delete((osmdb_relation_t**) &obj);
			}
		}

		pthread_mutex_destroy(&self->object_mutex);
		cc_map_delete(&self->object_map);
		cc_list_delete(&self->object_list);
		osmdb_database_finalizeSet(self,
		                           &self->stmt_select_ways_range);
		sqlite3_finalize(self->stmt_select_wnds);
		sqlite3_finalize(self->stmt_select_way);
		sqlite3_finalize(self->stmt_select_mways);
		sqlite3_finalize(self->stmt_select_mnodes);
		sqlite3_finalize(self->stmt_select_relation);
		osmdb_database_finalizeSet(self,
		                           &self->stmt_select_rels_range);
		sqlite3_finalize(self->stmt_select_node);
		osmdb_database_finalizeSet(self,
		                           &self->stmt_select_nodes_range);
		osmdb_database_finalizeSet(self, &self->stmt_search_rels);
		osmdb_database_finalizeSet(self, &self->stmt_search_ways);
		osmdb_database_finalizeSet(self, &self->stmt_search_nodes);
		osmdb_database_finalizeSet(self, &self->stmt_spellfix);

		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(sqlite3_shutdown() != SQLITE_OK)
		{
			LOGW("sqlite3_shutdown failed");
		}

		FREE(self);
		*_self = NULL;
	}
}

void osmdb_database_spellfix(osmdb_database_t* self,
                             int tid,
                             const char* text,
                             char* spellfix)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(spellfix);

	// initialize spellfix
	spellfix[0] = '\0';

	char words[256];
	snprintf(words, 256, "%s", text);

	int idx = 0;
	int len = 0;
	const char* word = words;
	while(1)
	{
		if(words[idx] == ' ')
		{
			words[idx] = '\0';
			osmdb_database_spellfixWord(self, tid, word,
			                            spellfix, &len);
			osmdb_database_cat(spellfix, &len, " ");
			word = &(words[idx + 1]);
		}
		else if(words[idx] == '\0')
		{
			osmdb_database_spellfixWord(self, tid, word,
			                            spellfix, &len);
			return;
		}

		++idx;
	}
}

int osmdb_database_search(osmdb_database_t* self,
                          int tid,
                          const char* text,
                          xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	xml_ostream_begin(os, "db");

	osmdb_database_searchTblNodes(self, tid, text, os);
	osmdb_database_searchTblWays(self, tid, text, os);
	osmdb_database_searchTblRels(self, tid, text, os);

	xml_ostream_end(os);

	return 1;
}

int osmdb_database_tile(osmdb_database_t* self,
                        int tid, int zoom, int x, int y,
                        xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(os);

	double latT;
	double lonL;
	double latB;
	double lonR;
	terrain_bounds(x, y, zoom, &latT, &lonL, &latB, &lonR);

	cc_map_t* map_export = cc_map_new();
	if(map_export == NULL)
	{
		return 0;
	}

	xml_ostream_begin(os, "osmdb");
	if(osmdb_database_gatherNodes(self, tid, zoom,
	                              latT, lonL, latB, lonR,
	                              map_export, os) == 0)
	{
		goto fail_gather_nodes;
	}

	if(osmdb_database_gatherRelations(self, tid, zoom,
	                                  latT, lonL, latB, lonR,
	                                  map_export, os) == 0)
	{
		goto fail_gather_relations;
	}

	if(osmdb_database_gatherWays(self, tid, zoom,
	                             latT, lonL, latB, lonR,
	                             map_export, os) == 0)
	{
		goto fail_gather_ways;
	}

	xml_ostream_end(os);

	cc_map_discard(map_export);
	cc_map_delete(&map_export);

	osmdb_database_trimCache(self);

	// success
	return 1;

	// failure
	fail_gather_ways:
	fail_gather_relations:
	fail_gather_nodes:
		cc_map_discard(map_export);
		cc_map_delete(&map_export);
	return 0;
}

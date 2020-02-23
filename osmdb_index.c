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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "../libcc/math/cc_vec2f.h"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "../libcc/cc_multimap.h"
#include "../libcc/cc_timestamp.h"
#include "../libcc/cc_unit.h"
#include "../terrain/terrain_tile.h"
#include "../terrain/terrain_util.h"
#include "osmdb_chunk.h"
#include "osmdb_index.h"
#include "osmdb_node.h"
#include "osmdb_relation.h"
#include "osmdb_tile.h"
#include "osmdb_util.h"
#include "osmdb_way.h"

const int OSMDB_INDEX_ONE = 1;

#define OSMDB_CHUNK_SIZE 400*1024*1024
#define OSMDB_TILE_SIZE  100*1024*1024

#define OSMDB_QUADRANT_NONE   0
#define OSMDB_QUADRANT_TOP    1
#define OSMDB_QUADRANT_LEFT   2
#define OSMDB_QUADRANT_BOTTOM 3
#define OSMDB_QUADRANT_RIGHT  4

/***********************************************************
* private - way                                            *
***********************************************************/

static int
osmdb_way_join(osmdb_way_t* a, osmdb_way_t* b,
               double ref1, double* ref2,
               struct osmdb_index_s* index)
{
	ASSERT(a);
	ASSERT(b);
	ASSERT(index);

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
	osmdb_node_t* node0;
	osmdb_node_t* node1;
	osmdb_node_t* node2;
	node0 = (osmdb_node_t*)
	        osmdb_index_find(index, OSMDB_TYPE_NODE, *refp);
	node1 = (osmdb_node_t*)
	        osmdb_index_find(index, OSMDB_TYPE_NODE, ref1);
	node2 = (osmdb_node_t*)
	        osmdb_index_find(index, OSMDB_TYPE_NODE, *refn);
	if((node0 == NULL) || (node1 == NULL) || (node2 == NULL))
	{
		return 0;
	}

	// check join angle to prevent joining ways
	// at a sharp angle since this causes weird
	// rendering artifacts
	cc_vec2f_t p0;
	cc_vec2f_t p1;
	cc_vec2f_t p2;
	cc_vec2f_t v01;
	cc_vec2f_t v12;
	terrain_coord2xy(node0->lat, node0->lon,
	                 &p0.x,  &p0.y);
	terrain_coord2xy(node1->lat, node1->lon,
	                 &p1.x,  &p1.y);
	terrain_coord2xy(node2->lat, node2->lon,
	                 &p2.x,  &p2.y);
	cc_vec2f_subv_copy(&p1, &p0, &v01);
	cc_vec2f_subv_copy(&p2, &p1, &v12);
	cc_vec2f_normalize(&v01);
	cc_vec2f_normalize(&v12);
	float dot = cc_vec2f_dot(&v01, &v12);
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

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_index_addJoin(osmdb_way_t* way,
                    cc_map_t* map_ways_work,
                    cc_multimap_t* mm_nds_join)
{
	ASSERT(way);
	ASSERT(map_ways_work);
	ASSERT(mm_nds_join);

	osmdb_way_t* copy = osmdb_way_copy(way);
	if(copy == NULL)
	{
		return 0;
	}

	if(cc_map_addf(map_ways_work, (const void*) copy, "%0.0lf",
	               copy->id) == 0)
	{
		osmdb_way_delete(&copy);
		return 0;
	}

	// check if way is complete
	double* ref1 = (double*) cc_list_peekHead(copy->nds);
	double* ref2 = (double*) cc_list_peekTail(copy->nds);
	if((ref1 == NULL) || (ref2 == NULL))
	{
		return 1;
	}

	double* id1_copy = (double*)
	                   MALLOC(sizeof(double));
	if(id1_copy == NULL)
	{
		return 0;
	}
	*id1_copy = copy->id;

	double* id2_copy = (double*)
	                   MALLOC(sizeof(double));
	if(id2_copy == NULL)
	{
		FREE(id1_copy);
		return 0;
	}
	*id2_copy = copy->id;

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

static void
osmdb_index_discardJoin(cc_map_t* map_ways_work,
                        cc_multimap_t* mm_nds_join)
{
	ASSERT(map_ways_work);
	ASSERT(mm_nds_join);

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(map_ways_work, &iterator);
	while(iter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*)
		      cc_map_remove(map_ways_work, &iter);
		osmdb_way_delete(&way);
	}

	cc_multimapIter_t  miterator;
	cc_multimapIter_t* miter;
	miter = cc_multimap_head(mm_nds_join, &miterator);
	while(miter)
	{
		double* ref;
		ref = (double*) cc_multimap_remove(mm_nds_join, &miter);
		FREE(ref);
	}
}

static void osmdb_index_computeMinDist(osmdb_index_t* self)
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
	cc_vec2f_t pa_8;
	cc_vec2f_t pb_8;
	cc_vec2f_t pa_11;
	cc_vec2f_t pb_11;
	cc_vec2f_t pa_14;
	cc_vec2f_t pb_14;
	terrain_coord2xy(latT_8, lonL_8,
	                 &pa_8.x, &pa_8.y);
	terrain_coord2xy(latB_8, lonR_8,
	                 &pb_8.x, &pb_8.y);
	terrain_coord2xy(latT_11, lonL_11,
	                 &pa_11.x, &pa_11.y);
	terrain_coord2xy(latB_11, lonR_11,
	                 &pb_11.x, &pb_11.y);
	terrain_coord2xy(latT_14, lonL_14,
	                 &pa_14.x, &pa_14.y);
	terrain_coord2xy(latB_14, lonR_14,
	                 &pb_14.x, &pb_14.y);

	// compute min_dist
	// scale by 1/8th since each tile serves 3 zoom levels
	float s   = 1.0f/8.0f;
	float pix = sqrtf(2*256.0f*256.0f);
	self->min_dist8  = s*cc_vec2f_distance(&pb_8, &pa_8)/pix;
	self->min_dist11 = s*cc_vec2f_distance(&pb_11, &pa_11)/pix;
	self->min_dist14 = s*cc_vec2f_distance(&pb_14, &pa_14)/pix;
	LOGI("min_dist8=%f, min_dist11=%f, min_dist14=%f",
	     self->min_dist8, self->min_dist11, self->min_dist14);
}

static cc_map_t*
osmdb_index_getMap(osmdb_index_t* self, int type)
{
	ASSERT(self);

	if(type == OSMDB_TYPE_NODE)
	{
		return self->map_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		return self->map_ways;
	}
	else if(type == OSMDB_TYPE_RELATION)
	{
		return self->map_relations;
	}
	else if(type == OSMDB_TYPE_CTRNODE)
	{
		return self->map_ctrnodes;
	}
	else if(type == OSMDB_TYPE_NODEREF)
	{
		return self->map_noderefs;
	}
	else if(type == OSMDB_TYPE_WAYREF)
	{
		return self->map_wayrefs;
	}
	else if(type == OSMDB_TYPE_CTRNODEREF)
	{
		return self->map_ctrnoderefs;
	}
	else if(type == OSMDB_TYPE_CTRWAYREF)
	{
		return self->map_ctrwayrefs;
	}

	LOGE("invalid type=%i", type);
	self->err = 1;
	return NULL;
}

static int
osmdb_index_flushChunks(osmdb_index_t* self, int type)
{
	ASSERT(self);

	cc_map_t* map = osmdb_index_getMap(self, type);
	if(map == NULL)
	{
		return 0;
	}

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(map, &iterator);
	while(iter)
	{
		cc_listIter_t* item = (cc_listIter_t*)
		                      cc_map_val(iter);
		osmdb_chunk_t* chunk = (osmdb_chunk_t*)
		                       cc_list_peekIter(item);
		if(osmdb_chunk_flush(chunk) == 0)
		{
			return 0;
		}
		iter = cc_map_next(iter);
	}

	return 1;
}

static void
osmdb_index_trimChunks(osmdb_index_t* self, int max_size)
{
	ASSERT(self);
	ASSERT(max_size >= 0);

	cc_listIter_t* item = cc_list_head(self->chunks);
	while(item)
	{
		if((max_size > 0) &&
		   ((self->size_chunks + self->size_map) <= max_size))
		{
			return;
		}

		double t0 = cc_timestamp();
		self->stats_chunk_trim += 1.0;
		if(max_size > 0)
		{
			self->stats_chunk_evict += 1.0;
		}

		osmdb_chunk_t* chunk;
		chunk = (osmdb_chunk_t*) cc_list_peekIter(item);
		if(chunk == NULL)
		{
			LOGE("invalid chunk");
			self->err = 1;
			return;
		}

		// get map attributes
		char key[256];
		cc_map_t* map = osmdb_index_getMap(self, chunk->type);
		if(map == NULL)
		{
			return;
		}
		snprintf(key, 256, "%0.0lf", chunk->idu);

		if(osmdb_chunk_locked(chunk))
		{
			self->stats_chunk_trim_dt += cc_timestamp() - t0;
			return;
		}

		// remove the chunk
		cc_mapIter_t  iterator;
		cc_mapIter_t* iter = &iterator;
		if(cc_map_find(map, iter, key) == NULL)
		{
			LOGE("invalid key=%s", key);
			self->err = 1;
			self->stats_chunk_trim_dt += cc_timestamp() - t0;
			return;
		}
		int hsz1 = (int) cc_map_sizeof(map);
		cc_map_remove(map, &iter);
		int hsz2 = (int) cc_map_sizeof(map);

		cc_list_remove(self->chunks, &item);

		// delete the chunk
		int dsize = 0;
		if(osmdb_chunk_delete(&chunk, &dsize) == 0)
		{
			self->err = 1;
		}
		self->size_chunks -= dsize;
		self->size_map   += hsz2 - hsz1;
		self->stats_chunk_trim_dt += cc_timestamp() - t0;
	}
}

static void
osmdb_index_trimTiles(osmdb_index_t* self, int max_size)
{
	ASSERT(self);
	ASSERT(max_size >= 0);

	cc_listIter_t* item = cc_list_head(self->tiles);
	while(item)
	{
		if((max_size > 0) &&
		   (self->size_tiles <= max_size))
		{
			return;
		}

		double t0 = cc_timestamp();
		self->stats_tile_trim += 1.0;
		if(max_size > 0)
		{
			self->stats_tile_evict += 1.0;
		}

		osmdb_tile_t* tile;
		tile = (osmdb_tile_t*) cc_list_peekIter(item);
		if(tile == NULL)
		{
			LOGE("invalid tile");
			self->err = 1;
			return;
		}

		char key[256];
		snprintf(key, 256, "Z%iX%iY%i",
		         tile->zoom, tile->x, tile->y);

		// remove the tile
		cc_mapIter_t  iterator;
		cc_mapIter_t* iter = &iterator;
		if(cc_map_find(self->map_tiles, iter, key) == NULL)
		{
			LOGE("invalid key=%s", key);
			self->err = 1;
			self->stats_tile_trim_dt += cc_timestamp() - t0;
			return;
		}
		cc_map_remove(self->map_tiles, &iter);
		cc_list_remove(self->tiles, &item);

		// delete the tile
		int dsize = osmdb_tile_size(tile);
		if(osmdb_tile_delete(&tile) == 0)
		{
			self->err = 1;
		}
		self->size_tiles -= dsize;
		self->stats_tile_trim_dt += cc_timestamp() - t0;
	}
}

static osmdb_chunk_t*
osmdb_index_getChunk(osmdb_index_t* self,
                     cc_map_t* map,
                     const char* key,
                     double idu, int type,
                     int find,
                     cc_listIter_t** list_iter)
{
	ASSERT(self);
	ASSERT(map);
	ASSERT(key);
	ASSERT(list_iter);

	double t0 = cc_timestamp();
	self->stats_chunk_get += 1.0;

	// check if chunk is already in map
	cc_mapIter_t hiter;
	osmdb_chunk_t*    chunk;
	cc_listIter_t*   iter;
	iter = (cc_listIter_t*) cc_map_find(map, &hiter, key);
	if(iter)
	{
		self->stats_chunk_hit += 1.0;
		chunk = (osmdb_chunk_t*) cc_list_peekIter(iter);
		cc_list_moven(self->chunks, iter, NULL);
	}
	else
	{
		self->stats_chunk_miss += 1.0;

		// import the chunk if it exists
		char fname[256];
		osmdb_chunk_fname(self->base, type, idu, fname);
		int exists = osmdb_fileExists(fname);
		if(find && (exists == 0))
		{
			// special case for find
			self->stats_chunk_get_dt += cc_timestamp() - t0;
			return NULL;
		}

		double load_t0 = cc_timestamp();
		if(exists)
		{
			self->stats_chunk_load += 1.0;
		}

		int csize;
		chunk = osmdb_chunk_new(self->base, idu, type,
		                        exists, &csize);
		if(chunk == NULL)
		{
			self->err = 1;
			self->stats_chunk_get_dt += cc_timestamp() - t0;
			return NULL;
		}

		if(exists)
		{
			self->stats_chunk_load_dt += cc_timestamp() - load_t0;
		}

		iter = cc_list_append(self->chunks,
		                       NULL, (const void*) chunk);
		if(iter == NULL)
		{
			goto fail_append;
		}

		int hsz1 = (int) cc_map_sizeof(map);
		if(cc_map_add(map, (const void*) iter,
		                   key) == 0)
		{
			goto fail_add;
		}
		int hsz2 = (int) cc_map_sizeof(map);
		self->size_chunks += csize;
		self->size_map   += hsz2 - hsz1;
		osmdb_index_trimChunks(self, OSMDB_CHUNK_SIZE);
	}

	// success
	*list_iter = iter;
	self->stats_chunk_get_dt += cc_timestamp() - t0;
	return chunk;

	// failure
	fail_add:
		cc_list_remove(self->chunks, &iter);
	fail_append:
	{
		int dsize;
		osmdb_chunk_delete(&chunk, &dsize);
		self->err = 1;
	}
	self->stats_chunk_get_dt += cc_timestamp() - t0;
	return NULL;
}

static osmdb_tile_t*
osmdb_index_getTile(osmdb_index_t* self,
                    int zoom, int x, int y,
                    const char* key,
                    cc_listIter_t** list_iter)
{
	ASSERT(self);
	ASSERT(key);
	ASSERT(list_iter);

	double t0 = cc_timestamp();
	self->stats_tile_get += 1.0;

	// check if tile is already in map
	cc_mapIter_t hiter;
	osmdb_tile_t*     tile;
	cc_listIter_t*   iter;
	iter = (cc_listIter_t*)
	       cc_map_find(self->map_tiles, &hiter, key);
	if(iter)
	{
		self->stats_tile_hit += 1.0;
		tile = (osmdb_tile_t*) cc_list_peekIter(iter);
		cc_list_moven(self->tiles, iter, NULL);
	}
	else
	{
		self->stats_tile_miss += 1.0;

		// import the tile if it exists
		char fname[256];
		osmdb_tile_fname(self->base, zoom, x, y, fname);
		int    exists  = osmdb_fileExists(fname);
		double load_t0 = cc_timestamp();
		if(exists)
		{
			self->stats_tile_load += 1.0;
		}

		tile = osmdb_tile_new(zoom, x, y,
		                      self->base, exists);
		if(tile == NULL)
		{
			self->err = 1;
			self->stats_tile_get_dt += cc_timestamp() - t0;
			return NULL;
		}

		if(exists)
		{
			self->stats_tile_load_dt += cc_timestamp() - load_t0;
		}

		iter = cc_list_append(self->tiles, NULL,
		                      (const void*) tile);
		if(iter == NULL)
		{
			goto fail_append;
		}

		if(cc_map_add(self->map_tiles, (const void*) iter,
		              key) == 0)
		{
			goto fail_add;
		}
		self->size_tiles += osmdb_tile_size(tile);
		osmdb_index_trimTiles(self, OSMDB_TILE_SIZE);
	}

	// success
	*list_iter = iter;
	self->stats_tile_get_dt += cc_timestamp() - t0;
	return tile;

	// failure
	fail_add:
		cc_list_remove(self->tiles, &iter);
	fail_append:
	{
		osmdb_tile_delete(&tile);
		self->err = 1;
	}
	self->stats_tile_get_dt += cc_timestamp() - t0;
	return NULL;
}

/***********************************************************
* public - osmdb_indexIter_t                               *
***********************************************************/

osmdb_indexIter_t*
osmdb_indexIter_new(struct osmdb_index_s* index, int type)
{
	ASSERT(index);

	if(osmdb_index_flushChunks(index, type) == 0)
	{
		return NULL;
	}

	osmdb_indexIter_t* self;
	self = (osmdb_indexIter_t*)
	       MALLOC(sizeof(osmdb_indexIter_t));
	if(self == NULL)
	{
		LOGE("MALLOC failed");
		return NULL;
	}

	char path[256];
	osmdb_chunk_path(index->base, type, path);

	self->dir = opendir(path);
	if(self->dir == NULL)
	{
		LOGE("invalid path=%s", path);
		goto fail_dir;
	}

	self->de = readdir(self->dir);
	if(self->de == NULL)
	{
		LOGE("invalid path=%s", path);
		goto fail_de;
	}

	self->index      = index;
	self->type       = type;
	self->chunk_iter = NULL;

	// success
	return osmdb_indexIter_next(self);

	// failure
	fail_de:
		closedir(self->dir);
	fail_dir:
		FREE(self);
	return NULL;
}

void osmdb_indexIter_delete(osmdb_indexIter_t** _self)
{
	ASSERT(_self);

	osmdb_indexIter_t* self = *_self;
	if(self)
	{
		closedir(self->dir);
		FREE(self);
		*_self = NULL;
	}
}

osmdb_indexIter_t* osmdb_indexIter_next(osmdb_indexIter_t* self)
{
	ASSERT(self);

	// get the next item in the chunk
	if(self->chunk_iter)
	{
		self->chunk_iter = cc_map_next(self->chunk_iter);
		if(self->chunk_iter)
		{
			cc_list_moven(self->index->chunks, self->list_iter,
			              NULL);
			return self;
		}
		else
		{
			osmdb_chunk_t* chunk;
			chunk = (osmdb_chunk_t*)
			        cc_list_peekIter(self->list_iter);
			osmdb_chunk_unlock(chunk);
			self->list_iter = NULL;
		}
	}

	// find the next chunk
	if(self->de)
	{
		if(self->de->d_type == DT_REG)
		{
			char name[256];
			snprintf(name, 256, "%s", self->de->d_name);

			char* ext = strstr(name, ".xml.gz");
			if(ext)
			{
				// extract idu from name
				ext[0] = '\0';
				double idu = strtod(name, NULL);

				// get map and key
				char key[256];
				cc_map_t* map;
				map = osmdb_index_getMap(self->index, self->type);
				if(map == NULL)
				{
					self->chunk_iter = NULL;
					self->de = readdir(self->dir);
					return osmdb_indexIter_next(self);
				}
				snprintf(key, 256, "%0.0lf", idu);

				// get the chunk
				cc_listIter_t* list_iter;
				osmdb_chunk_t*  chunk;
				chunk = osmdb_index_getChunk(self->index, map,
				                             key, idu, self->type, 0,
				                             &list_iter);
				if(chunk == NULL)
				{
					LOGE("invalid name=%s", self->de->d_name);
					self->chunk_iter = NULL;
					self->de = readdir(self->dir);
					self->index->err = 1;
					return osmdb_indexIter_next(self);
				}

				// get the chunk iterator
				self->chunk_iter = cc_map_head(chunk->map,
				                               &self->chunk_iterator);
				if(self->chunk_iter)
				{
					self->list_iter = list_iter;
					osmdb_chunk_lock(chunk);
					self->de = readdir(self->dir);
					return self;
				}
			}
		}

		// ignore directories, etc.
		self->de = readdir(self->dir);
		return osmdb_indexIter_next(self);
	}
	else
	{
		osmdb_indexIter_delete(&self);
	}
	return self;
}

const void* osmdb_indexIter_peek(osmdb_indexIter_t* self)
{
	ASSERT(self);

	if(self->chunk_iter == NULL)
	{
		return NULL;
	}

	return cc_map_val(self->chunk_iter);
}

static int
osmdb_index_sampleWay(osmdb_index_t* self,
                      int zoom,
                      osmdb_way_t* way)
{
	ASSERT(self);
	ASSERT(way);

	float min_dist;
	if(zoom == 14)
	{
		min_dist = self->min_dist14;
	}
	else if(zoom == 11)
	{
		min_dist = self->min_dist11;
	}
	else if(zoom == 8)
	{
		min_dist = self->min_dist8;
	}
	else
	{
		LOGW("invalid zoom=%i, id=%0.0lf",
		     zoom, way->id);
		return 1;
	}

	if(cc_list_size(way->nds) < 3)
	{
		return 1;
	}

	int first = 1;
	cc_vec2f_t p0  = { .x = 0.0f, .y=0.0f };
	cc_listIter_t* iter = cc_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*) cc_list_peekIter(iter);

		osmdb_node_t* node;
		node = (osmdb_node_t*)
		       osmdb_index_find(self, OSMDB_TYPE_NODE,
		                        *ref);
		if(node == NULL)
		{
			iter = cc_list_next(iter);
			continue;
		}

		// accept the last nd
		cc_listIter_t* next = cc_list_next(iter);
		if(next == NULL)
		{
			return 1;
		}

		// compute distance between points
		cc_vec2f_t p1;
		terrain_coord2xy(node->lat, node->lon,
		                 &p1.x, &p1.y);
		float dist = cc_vec2f_distance(&p1,  &p0);

		// check if the nd should be kept or discarded
		if(first || (dist >= min_dist))
		{
			cc_vec2f_copy(&p1, &p0);
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
	}

	return 1;
}

static int
osmdb_index_gatherNode(osmdb_index_t* self,
                       xml_ostream_t* os, double id,
                       cc_map_t* map_nodes)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_nodes);

	// check if id already included
	cc_mapIter_t iter;
	if(cc_map_findf(map_nodes, &iter, "%0.0lf", id))
	{
		return 1;
	}

	// node may not exist due to osmosis
	osmdb_node_t* node;
	node = (osmdb_node_t*)
	       osmdb_index_find(self, OSMDB_TYPE_NODE, id);
	if(node == NULL)
	{
		return 1;
	}

	// mark the node as found
	if(cc_map_addf(map_nodes, (const void*) &OSMDB_INDEX_ONE,
	               "%0.0lf", id) == 0)
	{
		return 0;
	}

	return osmdb_node_export(node, os);
}

static int
osmdb_index_gatherWay(osmdb_index_t* self,
                      xml_ostream_t* os,
                      double id, int zoom,
                      cc_map_t* map_nodes,
                      cc_map_t* map_ways)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_nodes);
	ASSERT(map_ways);

	// check if id already included
	cc_mapIter_t iterator;
	if(cc_map_findf(map_ways, &iterator, "%0.0lf", id))
	{
		return 1;
	}

	// way may not exist due to osmosis
	osmdb_way_t* way;
	way = (osmdb_way_t*)
	      osmdb_index_find(self, OSMDB_TYPE_WAY, id);
	if(way == NULL)
	{
		return 1;
	}

	osmdb_way_t* tmp = osmdb_way_copy(way);
	if(tmp == NULL)
	{
		return 0;
	}

	if(zoom == 14)
	{
		self->stats_sample_way14_total += (double)
		                                  cc_list_size(way->nds);
	}
	else if(zoom == 11)
	{
		self->stats_sample_way11_total += (double)
		                                  cc_list_size(way->nds);
	}
	else if(zoom == 8)
	{
		self->stats_sample_way8_total += (double)
		                                 cc_list_size(way->nds);
	}

	if(osmdb_index_sampleWay(self, zoom, tmp) == 0)
	{
		goto fail_sample;
	}

	if(zoom == 14)
	{
		self->stats_sample_way14_sample += (double)
		                                   cc_list_size(way->nds);
	}
	else if(zoom == 11)
	{
		self->stats_sample_way11_sample += (double)
		                                   cc_list_size(way->nds);
	}
	else if(zoom == 8)
	{
		self->stats_sample_way8_sample += (double)
		                                  cc_list_size(way->nds);
	}

	// gather nds
	cc_listIter_t* iter = cc_list_head(tmp->nds);
	while(iter)
	{
		double* ref = (double*) cc_list_peekIter(iter);
		if(osmdb_index_gatherNode(self, os, *ref,
		                          map_nodes) == 0)
		{
			goto fail_gather;
		}
		iter = cc_list_next(iter);
	}

	// mark the way as found
	if(cc_map_addf(map_ways, (const void*) &OSMDB_INDEX_ONE,
	               "%0.0lf", id) == 0)
	{
		goto fail_mark;
	}

	if(osmdb_way_export(tmp, os) == 0)
	{
		goto fail_export;
	}

	osmdb_way_delete(&tmp);

	// success
	return 1;

	// failure
	fail_export:
	fail_mark:
	fail_gather:
	fail_sample:
		osmdb_way_delete(&tmp);
	return 0;
}

static int
osmdb_index_fetchWay(osmdb_index_t* self,
                     xml_ostream_t* os,
                     double id,
                     cc_map_t* map_ways,
                     cc_map_t* map_ways_work,
                     cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_ways);
	ASSERT(map_ways_work);
	ASSERT(mm_nds_join);

	// check if id already included by a relation
	// which we don't want to join because it could
	// cause the relation shape to change
	cc_mapIter_t iterator;
	if(cc_map_findf(map_ways, &iterator, "%0.0lf", id))
	{
		return 1;
	}

	// way may not exist due to osmosis
	osmdb_way_t* way;
	way = (osmdb_way_t*)
	      osmdb_index_find(self, OSMDB_TYPE_WAY, id);
	if(way == NULL)
	{
		return 1;
	}

	if(osmdb_index_addJoin(way, map_ways_work,
	                       mm_nds_join) == 0)
	{
		return 0;
	}

	return 1;
}

static int
osmdb_index_joinWays(osmdb_index_t* self,
                     cc_map_t* map_ways_work,
                     cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(map_ways_work);
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
			       cc_map_findf(map_ways_work, &hiter1, "%0.0lf",
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
				       cc_map_findf(map_ways_work, hiter2, "%0.0lf",
				                    *id2);
				if(way2 == NULL)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				if(osmdb_way_join(way1, way2, ref1,
				                  &ref2, self) == 0)
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

				// remove way2 from map_ways_work
				cc_map_remove(map_ways_work, &hiter2);
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
osmdb_index_sampleWays(osmdb_index_t* self, int zoom,
                       cc_map_t* map_ways_work)
{
	ASSERT(self);
	ASSERT(map_ways_work);

	cc_mapIter_t  hiterator;
	cc_mapIter_t* hiter;
	hiter = cc_map_head(map_ways_work, &hiterator);
	while(hiter)
	{
		osmdb_way_t* way = (osmdb_way_t*) cc_map_val(hiter);
		if(zoom == 14)
		{
			self->stats_sample_way14_total += (double)
			                                  cc_list_size(way->nds);
		}
		else if(zoom == 11)
		{
			self->stats_sample_way11_total += (double)
			                                  cc_list_size(way->nds);
		}
		else if(zoom == 8)
		{
			self->stats_sample_way8_total += (double)
			                                 cc_list_size(way->nds);
		}

		if(osmdb_index_sampleWay(self, zoom, way) == 0)
		{
			return 0;
		}

		if(zoom == 14)
		{
			self->stats_sample_way14_sample += (double)
			                                   cc_list_size(way->nds);
		}
		else if(zoom == 11)
		{
			self->stats_sample_way11_sample += (double)
			                                   cc_list_size(way->nds);
		}
		else if(zoom == 8)
		{
			self->stats_sample_way8_sample += (double)
			                                  cc_list_size(way->nds);
		}

		hiter = cc_map_next(hiter);
	}

	return 1;
}

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
osmdb_index_clipWay(osmdb_index_t* self,
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
	osmdb_node_t*   node;
	cc_listIter_t* iter;
	cc_listIter_t* prev = NULL;
	iter = cc_list_head(way->nds);
	while(iter)
	{
		ref = (double*) cc_list_peekIter(iter);

		node = (osmdb_node_t*)
		       osmdb_index_find(self, OSMDB_TYPE_NODE, *ref);
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
			return;
		}

		q0   = q1;
		q1   = q2;
		prev = iter;
		iter = cc_list_next(iter);
	}
}

static int
osmdb_index_clipWays(osmdb_index_t* self,
                     int zoom, int x, int y,
                     cc_map_t* map_ways_work)
{
	ASSERT(self);
	ASSERT(map_ways_work);

	// compute the tile bounding box
	double latT;
	double lonL;
	double latB;
	double lonR;
	terrain_sample2coord(x, y, zoom,
	                     0, 0, &latT, &lonL);
	terrain_sample2coord(x, y, zoom,
	                     TERRAIN_SAMPLES_TILE - 1,
	                     TERRAIN_SAMPLES_TILE - 1,
	                     &latB, &lonR);

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

	cc_mapIter_t  hiterator;
	cc_mapIter_t* hiter;
	hiter = cc_map_head(map_ways_work, &hiterator);
	while(hiter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*) cc_map_val(hiter);

		self->stats_clip_unclipped += (double)
		                              cc_list_size(way->nds);

		osmdb_index_clipWay(self, way,
		                    latT, lonL, latB, lonR);

		self->stats_clip_clipped += (double)
		                            cc_list_size(way->nds);

		hiter = cc_map_next(hiter);
	}

	return 1;
}

static int
osmdb_index_exportWays(osmdb_index_t* self,
                       xml_ostream_t* os,
                       cc_map_t* map_ways_work,
                       cc_map_t* map_nodes)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_ways_work);
	ASSERT(map_nodes);

	cc_mapIter_t  hiterator;
	cc_mapIter_t* hiter;
	hiter = cc_map_head(map_ways_work, &hiterator);
	while(hiter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*) cc_map_val(hiter);

		// gather nds
		cc_listIter_t* iter = cc_list_head(way->nds);
		while(iter)
		{
			double* ref = (double*) cc_list_peekIter(iter);
			if(osmdb_index_gatherNode(self, os, *ref,
			                          map_nodes) == 0)
			{
				return 0;
			}
			iter = cc_list_next(iter);
		}

		if(osmdb_way_export(way, os) == 0)
		{
			return 0;
		}

		hiter = cc_map_next(hiter);
	}

	return 1;
}

static int
osmdb_index_gatherRelation(osmdb_index_t* self,
                           xml_ostream_t* os,
                           double id, int zoom,
                           cc_map_t* map_nodes,
                           cc_map_t* map_ways,
                           cc_map_t* map_relations)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_nodes);
	ASSERT(map_ways);
	ASSERT(map_relations);

	// check if id already included
	cc_mapIter_t iterator;
	if(cc_map_findf(map_relations, &iterator, "%0.0lf", id))
	{
		return 1;
	}

	// relation may not exist due to osmosis
	osmdb_relation_t* relation;
	relation = (osmdb_relation_t*)
	           osmdb_index_find(self, OSMDB_TYPE_RELATION, id);
	if(relation == NULL)
	{
		return 1;
	}

	// gather members
	cc_listIter_t* iter = cc_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    cc_list_peekIter(iter);
		if(m->type == OSMDB_TYPE_NODE)
		{
			if(osmdb_index_gatherNode(self, os, m->ref,
			                          map_nodes) == 0)
			{
				return 0;
			}
		}
		else if(m->type == OSMDB_TYPE_WAY)
		{
			if(osmdb_index_gatherWay(self, os, m->ref,
			                         zoom,
			                         map_nodes,
			                         map_ways) == 0)
			{
				return 0;
			}
		}
		iter = cc_list_next(iter);
	}

	// mark the relation as found
	if(cc_map_addf(map_relations,
	                    (const void*) &OSMDB_INDEX_ONE,
	                    "%0.0lf", id) == 0)
	{
		return 0;
	}

	return osmdb_relation_export(relation, os);
}

static int
osmdb_index_gatherTile(osmdb_index_t* self,
                       xml_ostream_t* os,
                       osmdb_tile_t* tile,
                       cc_map_t* map_nodes,
                       cc_map_t* map_ways,
                       cc_map_t* map_relations,
                       cc_map_t* map_ways_work,
                       cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(tile);
	ASSERT(map_nodes);
	ASSERT(map_ways);
	ASSERT(map_relations);
	ASSERT(map_ways_work);
	ASSERT(mm_nds_join);

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(tile->map_nodes, &iterator);
	while(iter)
	{
		double ref = strtod(cc_map_key(iter), NULL);

		if(osmdb_index_gatherNode(self, os, ref,
		                          map_nodes) == 0)
		{
			return 0;
		}

		iter = cc_map_next(iter);
	}

	iter = cc_map_head(tile->map_relations, &iterator);
	while(iter)
	{
		double ref = strtod(cc_map_key(iter), NULL);

		if(osmdb_index_gatherRelation(self, os, ref, tile->zoom,
		                              map_nodes,
		                              map_ways,
		                              map_relations) == 0)
		{
			return 0;
		}

		iter = cc_map_next(iter);
	}

	iter = cc_map_head(tile->map_ways, &iterator);
	while(iter)
	{
		double ref = strtod(cc_map_key(iter), NULL);

		if(osmdb_index_fetchWay(self, os, ref,
		                        map_ways,
		                        map_ways_work,
		                        mm_nds_join) == 0)
		{
			return 0;
		}

		iter = cc_map_next(iter);
	}

	if(osmdb_index_joinWays(self, map_ways_work,
	                        mm_nds_join) == 0)
	{
		return 0;
	}

	if(osmdb_index_sampleWays(self, tile->zoom,
	                          map_ways_work) == 0)
	{
		return 0;
	}

	if(osmdb_index_clipWays(self,
	                        tile->zoom, tile->x, tile->y,
	                        map_ways_work) == 0)
	{
		return 0;
	}

	if(osmdb_index_exportWays(self, os, map_ways_work,
	                          map_nodes) == 0)
	{
		return 0;
	}

	return 1;
}

static int osmdb_index_addTileXY(osmdb_index_t* self,
                                 int zoom, int x, int y,
                                 int type, double id)
{
	ASSERT(self);

	double t0 = cc_timestamp();
	self->stats_tile_add += 1.0;

	char key[256];
	snprintf(key, 256, "Z%iX%iY%i", zoom, x, y);

	// get the tile
	cc_listIter_t* list_iter;
	osmdb_tile_t* tile;
	tile = osmdb_index_getTile(self, zoom, x, y,
	                           key, &list_iter);
	if(tile == NULL)
	{
		// nothing to undo for this failure
		LOGE("invalid key=%s", key);
		self->err = 1;
		self->stats_tile_add_dt += cc_timestamp() - t0;
		return 0;
	}

	// find the data
	if(osmdb_tile_find(tile, type, id))
	{
		self->stats_tile_add_dt += cc_timestamp() - t0;
		return 1;
	}

	// add the data
	int tsz0 = osmdb_tile_size(tile);
	if(osmdb_tile_add(tile, type, id) == 0)
	{
		// nothing to undo for this failure
		LOGE("failure key=%s, type=%i, id=%0.0lf",
		     key, type, id);
		self->err = 1;
		self->stats_tile_add_dt += cc_timestamp() - t0;
		return 0;
	}
	int tsz1 = osmdb_tile_size(tile);
	self->size_tiles += tsz1 - tsz0;
	osmdb_index_trimTiles(self, OSMDB_TILE_SIZE);

	self->stats_tile_add_dt += cc_timestamp() - t0;
	return 1;
}

static int osmdb_index_addTile(osmdb_index_t* self,
                               osmdb_range_t* range,
                               int zoom,
                               int type, double id)
 {
	ASSERT(self);
	ASSERT(range);

	// ignore null range
	if(range->pts == 0)
	{
		return 1;
	}

	// compute the range
	float x0f;
	float y0f;
	float x1f;
	float y1f;
	terrain_coord2tile(range->latT, range->lonL,
	                   zoom, &x0f, &y0f);
	terrain_coord2tile(range->latB, range->lonR,
	                   zoom, &x1f, &y1f);

	// elements are defined with zero width but in
	// practice are drawn with non-zero width
	// points/lines so an offset is needed to ensure they
	// are not clipped between neighboring tiles
	float offset = 1.0f/16.0f;

	// add id to range
	int x;
	int y;
	int x0  = (int) (x0f - offset);
	int x1  = (int) (x1f + offset);
	int y0  = (int) (y0f - offset);
	int y1  = (int) (y1f + offset);
	int ret = 1;
	for(y = y0; y <= y1; ++y)
	{
		for(x = x0; x <= x1; ++x)
		{
			ret &= osmdb_index_addTileXY(self, zoom, x, y,
			                             type, id);
		}
	}

	// add to higher zoom levels
	if(zoom == 0)
	{
		ret &= osmdb_index_addTile(self, range, 5,
		                           type, id);
	}
	else if(zoom == 5)
	{
		ret &= osmdb_index_addTile(self, range, 8,
		                           type, id);
	}
	else if(zoom == 8)
	{
		ret &= osmdb_index_addTile(self, range, 11,
		                           type, id);
	}
	else if(zoom == 11)
	{
		ret &= osmdb_index_addTile(self, range, 14,
		                           type, id);
	}

	return ret;
 }

static void
osmdb_index_rangeWay(osmdb_index_t* self,
                     osmdb_way_t* way,
                     int center,
                     osmdb_range_t* range)
{
	ASSERT(self);
	ASSERT(way);
	ASSERT(range);

	osmdb_range_init(range);

	cc_listIter_t* iter = cc_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*) cc_list_peekIter(iter);

		osmdb_node_t* node;
		node = (osmdb_node_t*)
		       osmdb_index_find(self,
		                        OSMDB_TYPE_NODE,
		                        *ref);
		if(node == NULL)
		{
			if(center)
			{
				node = (osmdb_node_t*)
				       osmdb_index_find(self,
				                        OSMDB_TYPE_CTRNODE,
				                        *ref);
			}

			if(node == NULL)
			{
				iter = cc_list_next(iter);
				continue;
			}
		}

		osmdb_range_addPt(range, node->lat, node->lon);

		iter = cc_list_next(iter);
	}
}

static void
osmdb_index_rangeRelation(osmdb_index_t* self,
                          osmdb_relation_t* relation,
                          int center,
                          osmdb_range_t* range)
{
	ASSERT(self);
	ASSERT(relation);
	ASSERT(range);

	osmdb_range_init(range);

	cc_listIter_t* iter = cc_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    cc_list_peekIter(iter);

		if(m->type == OSMDB_TYPE_WAY)
		{
			osmdb_way_t* way;
			way = (osmdb_way_t*)
			       osmdb_index_find(self,
			                        OSMDB_TYPE_WAY,
			                        m->ref);
			if(way == NULL)
			{
				iter = cc_list_next(iter);
				continue;
			}

			osmdb_range_addPt(range, way->latT, way->lonL);
			osmdb_range_addPt(range, way->latB, way->lonR);
		}
		else if(center &&
		        (m->type == OSMDB_TYPE_NODE))
		{
			osmdb_node_t* node;
			node = (osmdb_node_t*)
			       osmdb_index_find(self,
			                        OSMDB_TYPE_NODE,
			                        m->ref);
			if(node == NULL)
			{
				if(center)
				{
					node = (osmdb_node_t*)
					       osmdb_index_find(self,
					                        OSMDB_TYPE_CTRNODE,
					                        m->ref);
				}

				if(node == NULL)
				{
					iter = cc_list_next(iter);
					continue;
				}
			}

			osmdb_range_init(range);
			osmdb_range_addPt(range, node->lat, node->lon);
			return;
		}

		iter = cc_list_next(iter);
	}
}

/***********************************************************
* public - osmdb_index_t                                   *
***********************************************************/

osmdb_index_t* osmdb_index_new(const char* base)
{
	ASSERT(base);

	osmdb_index_t* self = (osmdb_index_t*)
	                      MALLOC(sizeof(osmdb_index_t));
	if(self == NULL)
	{
		LOGE("MALLOC failed");
		return NULL;
	}

	self->chunks = cc_list_new();
	if(self->chunks == NULL)
	{
		goto fail_chunks;
	}

	self->map_nodes = cc_map_new();
	if(self->map_nodes == NULL)
	{
		goto fail_nodes;
	}

	self->map_ways = cc_map_new();
	if(self->map_ways == NULL)
	{
		goto fail_ways;
	}

	self->map_relations = cc_map_new();
	if(self->map_relations == NULL)
	{
		goto fail_relations;
	}

	self->map_ctrnodes = cc_map_new();
	if(self->map_ctrnodes == NULL)
	{
		goto fail_ctrnodes;
	}

	self->map_noderefs = cc_map_new();
	if(self->map_noderefs == NULL)
	{
		goto fail_noderefs;
	}

	self->map_wayrefs = cc_map_new();
	if(self->map_wayrefs == NULL)
	{
		goto fail_wayrefs;
	}

	self->map_ctrnoderefs = cc_map_new();
	if(self->map_ctrnoderefs == NULL)
	{
		goto fail_ctrnoderefs;
	}

	self->map_ctrwayrefs = cc_map_new();
	if(self->map_ctrwayrefs == NULL)
	{
		goto fail_ctrwayrefs;
	}

	self->tiles = cc_list_new();
	if(self->tiles == NULL)
	{
		goto fail_tiles;
	}

	self->map_tiles = cc_map_new();
	if(self->map_tiles == NULL)
	{
		goto fail_map_tiles;
	}

	snprintf(self->base, 256, "%s", base);
	self->size_chunks               = 0;
	self->size_map                 = 0;
	self->size_tiles                = 0;
	self->err                       = 0;
	self->stats_chunk_hit           = 0.0;
	self->stats_chunk_miss          = 0.0;
	self->stats_chunk_evict         = 0.0;
	self->stats_chunk_add           = 0.0;
	self->stats_chunk_add_dt        = 0.0;
	self->stats_chunk_find          = 0.0;
	self->stats_chunk_find_dt       = 0.0;
	self->stats_chunk_get           = 0.0;
	self->stats_chunk_get_dt        = 0.0;
	self->stats_chunk_load          = 0.0;
	self->stats_chunk_load_dt       = 0.0;
	self->stats_chunk_trim          = 0.0;
	self->stats_chunk_trim_dt       = 0.0;
	self->stats_tile_hit            = 0.0;
	self->stats_tile_miss           = 0.0;
	self->stats_tile_evict          = 0.0;
	self->stats_tile_add            = 0.0;
	self->stats_tile_add_dt         = 0.0;
	self->stats_tile_make           = 0.0;
	self->stats_tile_make_dt        = 0.0;
	self->stats_tile_get            = 0.0;
	self->stats_tile_get_dt         = 0.0;
	self->stats_tile_load           = 0.0;
	self->stats_tile_load_dt        = 0.0;
	self->stats_tile_trim           = 0.0;
	self->stats_tile_trim_dt        = 0.0;
	self->stats_sample_way8_sample  = 0.0;
	self->stats_sample_way8_total   = 0.0;
	self->stats_sample_way11_sample = 0.0;
	self->stats_sample_way11_total  = 0.0;
	self->stats_sample_way14_sample = 0.0;
	self->stats_sample_way14_total  = 0.0;
	self->stats_clip_unclipped      = 0.0;
	self->stats_clip_clipped        = 0.0;

	osmdb_index_computeMinDist(self);

	// success
	return self;

	// failure
	fail_map_tiles:
		cc_list_delete(&self->tiles);
	fail_tiles:
		cc_map_delete(&self->map_ctrwayrefs);
	fail_ctrwayrefs:
		cc_map_delete(&self->map_ctrnoderefs);
	fail_ctrnoderefs:
		cc_map_delete(&self->map_wayrefs);
	fail_wayrefs:
		cc_map_delete(&self->map_noderefs);
	fail_noderefs:
		cc_map_delete(&self->map_ctrnodes);
	fail_ctrnodes:
		cc_map_delete(&self->map_relations);
	fail_relations:
		cc_map_delete(&self->map_ways);
	fail_ways:
		cc_map_delete(&self->map_nodes);
	fail_nodes:
		cc_list_delete(&self->chunks);
	fail_chunks:
		FREE(self);
	return NULL;
}

int osmdb_index_delete(osmdb_index_t** _self)
{
	ASSERT(_self);

	int err = 0;

	osmdb_index_t* self = *_self;
	if(self)
	{
		osmdb_index_trimChunks(self, 0);
		osmdb_index_trimTiles(self, 0);
		cc_map_delete(&self->map_tiles);
		cc_list_delete(&self->tiles);
		cc_map_delete(&self->map_ctrwayrefs);
		cc_map_delete(&self->map_ctrnoderefs);
		cc_map_delete(&self->map_wayrefs);
		cc_map_delete(&self->map_noderefs);
		cc_map_delete(&self->map_ctrnodes);
		cc_map_delete(&self->map_relations);
		cc_map_delete(&self->map_ways);
		cc_map_delete(&self->map_nodes);
		cc_list_delete(&self->chunks);
		err = self->err;
		osmdb_index_stats(self);
		FREE(self);
		*_self = NULL;
	}

	return err ? 0 : 1;
}

int osmdb_index_error(osmdb_index_t* self)
{
	ASSERT(self);

	return self->err;
}

int osmdb_index_addChunk(osmdb_index_t* self,
                         int type, const void* data)
{
	ASSERT(self);
	ASSERT(data);

	double t0 = cc_timestamp();
	self->stats_chunk_add += 1.0;

	// get data/map attributes
	int    dsize;
	double id;
	double idu = 0.0;
	double idl = 0.0;
	char   key[256];
	cc_map_t*         map;
	osmdb_node_t*     node     = NULL;
	osmdb_way_t*      way      = NULL;
	osmdb_relation_t* relation = NULL;
	double*           ref      = NULL;
	if(type == OSMDB_TYPE_NODE)
	{
		node  = (osmdb_node_t*) data;
		map  = self->map_nodes;
		id    = node->id;
		dsize = osmdb_node_size(node);
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		way   = (osmdb_way_t*) data;
		map  = self->map_ways;
		id    = way->id;
		dsize = osmdb_way_size(way);
	}
	else if(type == OSMDB_TYPE_RELATION)
	{
		relation = (osmdb_relation_t*) data;
		map     = self->map_relations;
		id       = relation->id;
		dsize    = osmdb_relation_size(relation);
	}
	else if(type == OSMDB_TYPE_CTRNODE)
	{
		node = (osmdb_node_t*) data;
		map = self->map_ctrnodes;
		id   = node->id;
		dsize = osmdb_node_size(node);
	}
	else if(type == OSMDB_TYPE_NODEREF)
	{
		ref   = (double*) data;
		map  = self->map_noderefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else if(type == OSMDB_TYPE_WAYREF)
	{
		ref   = (double*) data;
		map  = self->map_wayrefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else if(type == OSMDB_TYPE_CTRNODEREF)
	{
		ref   = (double*) data;
		map  = self->map_ctrnoderefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else if(type == OSMDB_TYPE_CTRWAYREF)
	{
		ref   = (double*) data;
		map  = self->map_ctrwayrefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else
	{
		self->stats_chunk_add_dt += cc_timestamp() - t0;
		LOGE("invalid type=%i", type);
		return 0;
	}
	osmdb_splitId(id, &idu, &idl);
	snprintf(key, 256, "%0.0lf", idu);

	// check if the data already exists
	if(osmdb_index_find(self, type, id))
	{
		if((type == OSMDB_TYPE_NODE) ||
		   (type == OSMDB_TYPE_CTRNODE))
		{
			osmdb_node_delete(&node);
		}
		else if(type == OSMDB_TYPE_WAY)
		{
			osmdb_way_delete(&way);
		}
		else if(type == OSMDB_TYPE_RELATION)
		{
			osmdb_relation_delete(&relation);
		}
		else if((type == OSMDB_TYPE_NODEREF)    ||
		        (type == OSMDB_TYPE_CTRNODEREF) ||
		        (type == OSMDB_TYPE_WAYREF)     ||
		        (type == OSMDB_TYPE_CTRWAYREF))
		{
			FREE(ref);
		}
		else
		{
			self->stats_chunk_add_dt += cc_timestamp() - t0;
			LOGE("invalid type=%i", type);
			return 0;
		}

		self->stats_chunk_add_dt += cc_timestamp() - t0;
		return 1;
	}

	// get the chunk
	cc_listIter_t* list_iter;
	osmdb_chunk_t* chunk;
	chunk = osmdb_index_getChunk(self, map,
	                             key, idu, type, 0,
	                             &list_iter);
	if(chunk == NULL)
	{
		// nothing to undo for this failure
		LOGE("invalid id=%0.0lf, idu=%0.0lf, idl=%0.0lf",
		     id, idu, idl);
		self->err = 1;
		self->stats_chunk_add_dt += cc_timestamp() - t0;
		return 0;
	}

	// add the data
	if(osmdb_chunk_add(chunk, data, idl, dsize) == 0)
	{
		// nothing to undo for this failure
		LOGE("failure key=%s, type=%i, id=%0.0lf, idu=%0.0lf, idl=%0.0lf",
		     key, type, id, idu, idl);
		self->err = 1;
		self->stats_chunk_add_dt += cc_timestamp() - t0;
		return 0;
	}
	self->size_chunks += dsize;
	osmdb_index_trimChunks(self, OSMDB_CHUNK_SIZE);

	self->stats_chunk_add_dt += cc_timestamp() - t0;
	return 1;
}

int osmdb_index_addNode(osmdb_index_t* self,
                        int zoom, int center,
                        int selected,
                        osmdb_node_t* node)
{
	ASSERT(self);
	ASSERT(node);

	if(selected)
	{
		osmdb_range_t range;
		osmdb_range_init(&range);
		osmdb_range_addPt(&range, node->lat, node->lon);

		if(osmdb_index_addTile(self, &range, zoom,
		                       OSMDB_TYPE_NODE, node->id) == 0)
		{
			return 0;
		}
	}

	if(center)
	{
		return osmdb_index_addChunk(self,
		                            OSMDB_TYPE_CTRNODE,
		                            (const void*) node);
	}
	else
	{
		return osmdb_index_addChunk(self,
		                            OSMDB_TYPE_NODE,
		                            (const void*) node);
	}
}

int osmdb_index_addWay(osmdb_index_t* self,
                       int zoom,
                       int center,
                       int selected,
                       osmdb_way_t* way)
{
	ASSERT(self);
	ASSERT(way);

	// discard any ways w/o any points
	osmdb_range_t range;
	osmdb_index_rangeWay(self, way, center, &range);
	if(range.pts == 0)
	{
		osmdb_way_delete(&way);
		return 1;
	}
	osmdb_way_updateRange(way, &range);

	if(center)
	{
		osmdb_way_discardNds(way);
	}

	if(selected)
	{
		if(osmdb_index_addTile(self, &range, zoom,
		                       OSMDB_TYPE_WAY, way->id) == 0)
		{
			return 0;
		}
	}

	return osmdb_index_addChunk(self, OSMDB_TYPE_WAY,
	                            (const void*) way);
}

int osmdb_index_addRelation(osmdb_index_t* self,
                            int zoom, int center,
                            osmdb_relation_t* relation)
{
	ASSERT(self);
	ASSERT(relation);

	// discard relations w/o any points
	osmdb_range_t range;
	osmdb_index_rangeRelation(self, relation,
	                          center, &range);
	if(range.pts == 0)
	{
		osmdb_relation_delete(&relation);
		return 1;
	}
	osmdb_relation_updateRange(relation, &range);

	if(center)
	{
		osmdb_relation_discardMembers(relation);
	}

	if(osmdb_index_addTile(self, &range, zoom,
	                       OSMDB_TYPE_RELATION,
	                       relation->id) == 0)
	{
		return 0;
	}

	return osmdb_index_addChunk(self, OSMDB_TYPE_RELATION,
	                            (const void*) relation);
}

int osmdb_index_makeTile(osmdb_index_t* self,
                         int zoom, int x, int y,
                         xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(os);

	double t0 = cc_timestamp();
	self->stats_tile_make += 1.0;

	char key[256];
	snprintf(key, 256, "Z%iX%iY%i", zoom, x, y);

	// get the tile
	cc_listIter_t* list_iter;
	osmdb_tile_t*  tile;
	tile = osmdb_index_getTile(self, zoom, x, y,
	                           key, &list_iter);
	if(tile == NULL)
	{
		// this error doesn't affect the consistency
		// of the database so don't set the err flag
		LOGE("invalid key=%s", key);
		self->stats_tile_make_dt += cc_timestamp() - t0;
		return 0;
	}

	// allocate temporary mapes
	// which are needed to avoid duplicate entries
	// maps from ids to OSMDB_INDEX_ONE
	cc_map_t* map_nodes = cc_map_new();
	if(map_nodes == NULL)
	{
		goto fail_nodes;
	}

	cc_map_t* map_ways = cc_map_new();
	if(map_ways == NULL)
	{
		goto fail_ways;
	}

	cc_map_t* map_relations = cc_map_new();
	if(map_relations == NULL)
	{
		goto fail_relations;
	}

	cc_map_t* map_ways_work = cc_map_new();
	if(map_ways_work == NULL)
	{
		goto fail_ways_join;
	}

	cc_multimap_t* mm_nds_join = cc_multimap_new(NULL);
	if(mm_nds_join == NULL)
	{
		goto fail_nds_join;
	}

	// stream data
	int ret = xml_ostream_begin(os, "osmdb");
	if(osmdb_index_gatherTile(self, os, tile,
	                          map_nodes, map_ways,
	                          map_relations,
	                          map_ways_work,
	                          mm_nds_join) == 0)
	{
		goto fail_gather;
	}
	ret &= xml_ostream_end(os);

	// check if stream is complete
	if((ret == 0) || (xml_ostream_complete(os) == 0))
	{
		goto fail_os;
	}

	osmdb_index_discardJoin(map_ways_work,
	                        mm_nds_join);
	cc_map_discard(map_nodes);
	cc_map_discard(map_ways);
	cc_map_discard(map_relations);
	cc_multimap_delete(&mm_nds_join);
	cc_map_delete(&map_ways_work);
	cc_map_delete(&map_relations);
	cc_map_delete(&map_ways);
	cc_map_delete(&map_nodes);

	self->stats_tile_make_dt += cc_timestamp() - t0;

	// success
	return 1;

	// failure
	fail_os:
	fail_gather:
		osmdb_index_discardJoin(map_ways_work,
		                        mm_nds_join);
		cc_map_discard(map_relations);
		cc_map_discard(map_ways);
		cc_map_discard(map_nodes);
		cc_multimap_delete(&mm_nds_join);
	fail_nds_join:
		cc_map_delete(&map_ways_work);
	fail_ways_join:
		cc_map_delete(&map_relations);
	fail_relations:
		cc_map_delete(&map_ways);
	fail_ways:
		cc_map_delete(&map_nodes);
	fail_nodes:
		self->stats_tile_make_dt += cc_timestamp() - t0;
	return 0;
}

const void* osmdb_index_find(osmdb_index_t* self,
                             int type, double id)
{
	ASSERT(self);

	double t0 = cc_timestamp();
	self->stats_chunk_find += 1.0;

	// get map attributes
	double idu;
	double idl;
	char   key[256];
	cc_map_t* map = osmdb_index_getMap(self, type);
	if(map == NULL)
	{
		self->stats_chunk_find_dt += cc_timestamp() - t0;
		return NULL;
	}
	osmdb_splitId(id, &idu, &idl);
	snprintf(key, 256, "%0.0lf", idu);

	// get the chunk
	cc_listIter_t* list_iter;
	osmdb_chunk_t* chunk;
	chunk = osmdb_index_getChunk(self, map,
	                             key, idu, type, 1,
	                             &list_iter);
	if(chunk == NULL)
	{
		// data not found or an error occurred in getChunk
		// don't set the err flag here
		self->stats_chunk_find_dt += cc_timestamp() - t0;
		return NULL;
	}

	// find the data
	const void* data = osmdb_chunk_find(chunk, idl);
	self->stats_chunk_find_dt += cc_timestamp() - t0;
	return data;
}

void osmdb_index_stats(osmdb_index_t* self)
{
	ASSERT(self);

	LOGI("STATS: %s", self->base);
	LOGI("==CHUNK==");
	LOGI("HIT/MISS/EVICT: %0.0lf, %0.0lf, %0.0lf",
	     self->stats_chunk_hit, self->stats_chunk_miss, self->stats_chunk_evict);
	LOGI("ADD:  cnt=%0.0lf, dt=%lf", self->stats_chunk_add, self->stats_chunk_add_dt);
	LOGI("FIND: cnt=%0.0lf, dt=%lf", self->stats_chunk_find, self->stats_chunk_find_dt);
	LOGI("GET:  cnt=%0.0lf, dt=%lf", self->stats_chunk_get, self->stats_chunk_get_dt);
	LOGI("LOAD: cnt=%0.0lf, dt=%lf", self->stats_chunk_load, self->stats_chunk_load_dt);
	LOGI("TRIM: cnt=%0.0lf, dt=%lf", self->stats_chunk_trim, self->stats_chunk_trim_dt);
	LOGI("==TILE==");
	LOGI("HIT/MISS/EVICT: %0.0lf, %0.0lf, %0.0lf",
	     self->stats_tile_hit, self->stats_tile_miss, self->stats_tile_evict);
	LOGI("ADD:  cnt=%0.0lf, dt=%lf", self->stats_tile_add, self->stats_tile_add_dt);
	LOGI("MAKE: cnt=%0.0lf, dt=%lf", self->stats_tile_make, self->stats_tile_make_dt);
	LOGI("GET:  cnt=%0.0lf, dt=%lf", self->stats_tile_get, self->stats_tile_get_dt);
	LOGI("LOAD: cnt=%0.0lf, dt=%lf", self->stats_tile_load, self->stats_tile_load_dt);
	LOGI("TRIM: cnt=%0.0lf, dt=%lf", self->stats_tile_trim, self->stats_tile_trim_dt);
	LOGI("==SAMPLE==");
	LOGI("WAY8/11/14: %0.0lf/%0.0lf, %0.0lf/%0.0lf, %0.0lf/%0.0lf",
	     self->stats_sample_way8_sample,
	     self->stats_sample_way8_total,
	     self->stats_sample_way11_sample,
	     self->stats_sample_way11_total,
	     self->stats_sample_way14_sample,
	     self->stats_sample_way14_total);
	LOGI("==CLIP==");
	LOGI("CLIPPED/UNCLIPPED: %0.0lf/%0.0lf",
	     self->stats_clip_clipped,
	     self->stats_clip_unclipped);
}

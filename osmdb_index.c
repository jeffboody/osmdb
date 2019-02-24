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
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../a3d/a3d_timestamp.h"
#include "../a3d/a3d_multimap.h"
#include "../a3d/math/a3d_vec2f.h"
#include "../terrain/terrain_util.h"
#include "osmdb_chunk.h"
#include "osmdb_tile.h"
#include "osmdb_index.h"
#include "osmdb_node.h"
#include "osmdb_way.h"
#include "osmdb_relation.h"
#include "osmdb_util.h"

#define LOG_TAG "osmdb"
#include "../libxmlstream/xml_log.h"

const int OSMDB_INDEX_ONE = 1;

#define OSMDB_CHUNK_SIZE 400*1024*1024
#define OSMDB_TILE_SIZE  100*1024*1024

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_index_addJoin(osmdb_way_t* way,
                    a3d_hashmap_t* hash_ways_join,
                    a3d_multimap_t* mm_nds_join)
{
	assert(way);
	assert(hash_ways_join);
	assert(mm_nds_join);

	osmdb_way_t* copy = osmdb_way_copy(way);
	if(copy == NULL)
	{
		return 0;
	}

	if(a3d_hashmap_addf(hash_ways_join,
	                    (const void*) copy,
	                    "%0.0lf", copy->id) == 0)
	{
		osmdb_way_delete(&copy);
		return 0;
	}

	// check if way is complete
	double* ref1 = (double*) a3d_list_peekhead(copy->nds);
	double* ref2 = (double*) a3d_list_peektail(copy->nds);
	if((ref1 == NULL) || (ref2 == NULL))
	{
		return 1;
	}

	double* id1_copy = (double*)
	                   malloc(sizeof(double));
	if(id1_copy == NULL)
	{
		return 0;
	}
	*id1_copy = copy->id;

	double* id2_copy = (double*)
	                   malloc(sizeof(double));
	if(id2_copy == NULL)
	{
		free(id1_copy);
		return 0;
	}
	*id2_copy = copy->id;

	if(a3d_multimap_addf(mm_nds_join, (const void*) id1_copy,
	                     "%0.0lf", *ref1) == 0)
	{
		free(id1_copy);
		free(id2_copy);
		return 0;
	}

	if(a3d_multimap_addf(mm_nds_join, (const void*) id2_copy,
	                     "%0.0lf", *ref2) == 0)
	{
		free(id2_copy);
		return 0;
	}

	return 1;
}

static void
osmdb_index_discardJoin(a3d_hashmap_t* hash_ways_join,
                        a3d_multimap_t* mm_nds_join)
{
	assert(hash_ways_join);
	assert(mm_nds_join);

	a3d_hashmapIter_t  iterator;
	a3d_hashmapIter_t* iter;
	iter = a3d_hashmap_head(hash_ways_join, &iterator);
	while(iter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*)
		      a3d_hashmap_remove(hash_ways_join, &iter);
		osmdb_way_delete(&way);
	}

	a3d_multimapIter_t  miterator;
	a3d_multimapIter_t* miter;
	miter = a3d_multimap_head(mm_nds_join, &miterator);
	while(miter)
	{
		double* ref;
		ref = (double*)
		      a3d_multimap_remove(mm_nds_join, &miter);
		free(ref);
	}
}

static void osmdb_index_computeMinDist(osmdb_index_t* self)
{
	assert(self);

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
	a3d_vec2f_t pa_8;
	a3d_vec2f_t pb_8;
	a3d_vec2f_t pa_11;
	a3d_vec2f_t pb_11;
	a3d_vec2f_t pa_14;
	a3d_vec2f_t pb_14;
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
	float pm8        = 16.0f;
	float pm11       = 4.0f;
	float pm14       = 1.0f/4.0f;
	float pix        = sqrtf(2*256.0f*256.0f);
	self->min_dist8  = pm8*a3d_vec2f_distance(&pb_8, &pa_8)/pix;
	self->min_dist11 = pm11*a3d_vec2f_distance(&pb_11, &pa_11)/pix;
	self->min_dist14 = pm14*a3d_vec2f_distance(&pb_14, &pa_14)/pix;
	LOGI("min_dist8=%f, min_dist11=%f, min_dist14=%f",
	     self->min_dist8, self->min_dist11, self->min_dist14);
}

static a3d_hashmap_t*
osmdb_index_getHash(osmdb_index_t* self, int type)
{
	assert(self);

	if(type == OSMDB_TYPE_NODE)
	{
		return self->hash_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		return self->hash_ways;
	}
	else if(type == OSMDB_TYPE_RELATION)
	{
		return self->hash_relations;
	}
	else if(type == OSMDB_TYPE_CTRNODE)
	{
		return self->hash_ctrnodes;
	}
	else if(type == OSMDB_TYPE_NODEREF)
	{
		return self->hash_noderefs;
	}
	else if(type == OSMDB_TYPE_WAYREF)
	{
		return self->hash_wayrefs;
	}
	else if(type == OSMDB_TYPE_CTRNODEREF)
	{
		return self->hash_ctrnoderefs;
	}
	else if(type == OSMDB_TYPE_CTRWAYREF)
	{
		return self->hash_ctrwayrefs;
	}

	LOGE("invalid type=%i", type);
	self->err = 1;
	return NULL;
}

static int
osmdb_index_flushChunks(osmdb_index_t* self, int type)
{
	assert(self);

	a3d_hashmap_t* hash = osmdb_index_getHash(self, type);
	if(hash == NULL)
	{
		return 0;
	}

	a3d_hashmapIter_t  iterator;
	a3d_hashmapIter_t* iter;
	iter = a3d_hashmap_head(hash, &iterator);
	while(iter)
	{
		a3d_listitem_t* item = (a3d_listitem_t*)
		                       a3d_hashmap_val(iter);
		osmdb_chunk_t* chunk = (osmdb_chunk_t*)
		                       a3d_list_peekitem(item);
		if(osmdb_chunk_flush(chunk) == 0)
		{
			return 0;
		}
		iter = a3d_hashmap_next(iter);
	}

	return 1;
}

static void
osmdb_index_trimChunks(osmdb_index_t* self, int max_size)
{
	assert(self);
	assert(max_size >= 0);

	a3d_listitem_t* item = a3d_list_head(self->chunks);
	while(item)
	{
		if((max_size > 0) &&
		   ((self->size_chunks + self->size_hash) <= max_size))
		{
			return;
		}

		double t0 = a3d_timestamp();
		self->stats_chunk_trim += 1.0;
		if(max_size > 0)
		{
			self->stats_chunk_evict += 1.0;
		}

		osmdb_chunk_t* chunk = (osmdb_chunk_t*)
		                       a3d_list_peekitem(item);
		if(chunk == NULL)
		{
			LOGE("invalid chunk");
			self->err = 1;
			return;
		}

		// get hash attributes
		char key[256];
		a3d_hashmap_t* hash = osmdb_index_getHash(self, chunk->type);
		if(hash == NULL)
		{
			return;
		}
		snprintf(key, 256, "%0.0lf", chunk->idu);

		if(osmdb_chunk_locked(chunk))
		{
			self->stats_chunk_trim_dt += a3d_timestamp() - t0;
			return;
		}

		// remove the chunk
		a3d_hashmapIter_t  iterator;
		a3d_hashmapIter_t* iter = &iterator;
		if(a3d_hashmap_find(hash, iter, key) == NULL)
		{
			LOGE("invalid key=%s", key);
			self->err = 1;
			self->stats_chunk_trim_dt += a3d_timestamp() - t0;
			return;
		}
		int hsz1 = a3d_hashmap_hashmapSize(hash);
		a3d_hashmap_remove(hash, &iter);
		int hsz2 = a3d_hashmap_hashmapSize(hash);

		a3d_list_remove(self->chunks, &item);

		// delete the chunk
		int dsize = 0;
		if(osmdb_chunk_delete(&chunk, &dsize) == 0)
		{
			self->err = 1;
		}
		self->size_chunks -= dsize;
		self->size_hash   += hsz2 - hsz1;
		self->stats_chunk_trim_dt += a3d_timestamp() - t0;
	}
}

static void
osmdb_index_trimTiles(osmdb_index_t* self, int max_size)
{
	assert(self);
	assert(max_size >= 0);

	a3d_listitem_t* item = a3d_list_head(self->tiles);
	while(item)
	{
		if((max_size > 0) &&
		   (self->size_tiles <= max_size))
		{
			return;
		}

		double t0 = a3d_timestamp();
		self->stats_tile_trim += 1.0;
		if(max_size > 0)
		{
			self->stats_tile_evict += 1.0;
		}

		osmdb_tile_t* tile = (osmdb_tile_t*)
		                     a3d_list_peekitem(item);
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
		a3d_hashmapIter_t  iterator;
		a3d_hashmapIter_t* iter = &iterator;
		if(a3d_hashmap_find(self->hash_tiles, iter, key) == NULL)
		{
			LOGE("invalid key=%s", key);
			self->err = 1;
			self->stats_tile_trim_dt += a3d_timestamp() - t0;
			return;
		}
		a3d_hashmap_remove(self->hash_tiles, &iter);
		a3d_list_remove(self->tiles, &item);

		// delete the tile
		int dsize = osmdb_tile_size(tile);
		if(osmdb_tile_delete(&tile) == 0)
		{
			self->err = 1;
		}
		self->size_tiles -= dsize;
		self->stats_tile_trim_dt += a3d_timestamp() - t0;
	}
}

static osmdb_chunk_t*
osmdb_index_getChunk(osmdb_index_t* self,
                     a3d_hashmap_t* hash,
                     const char* key,
                     double idu, int type,
                     int find,
                     a3d_listitem_t** list_iter)
{
	assert(self);
	assert(hash);
	assert(key);
	assert(list_iter);

	double t0 = a3d_timestamp();
	self->stats_chunk_get += 1.0;

	// check if chunk is already in hash
	a3d_hashmapIter_t hiter;
	osmdb_chunk_t*    chunk;
	a3d_listitem_t*   iter;
	iter = (a3d_listitem_t*)
	       a3d_hashmap_find(hash, &hiter, key);
	if(iter)
	{
		self->stats_chunk_hit += 1.0;
		chunk = (osmdb_chunk_t*) a3d_list_peekitem(iter);
		a3d_list_moven(self->chunks, iter, NULL);
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
			self->stats_chunk_get_dt += a3d_timestamp() - t0;
			return NULL;
		}

		double load_t0 = a3d_timestamp();
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
			self->stats_chunk_get_dt += a3d_timestamp() - t0;
			return NULL;
		}

		if(exists)
		{
			self->stats_chunk_load_dt += a3d_timestamp() - load_t0;
		}

		iter = a3d_list_append(self->chunks,
		                       NULL, (const void*) chunk);
		if(iter == NULL)
		{
			goto fail_append;
		}

		int hsz1 = a3d_hashmap_hashmapSize(hash);
		if(a3d_hashmap_add(hash, (const void*) iter,
		                   key) == 0)
		{
			goto fail_add;
		}
		int hsz2 = a3d_hashmap_hashmapSize(hash);
		self->size_chunks += csize;
		self->size_hash   += hsz2 - hsz1;
		osmdb_index_trimChunks(self, OSMDB_CHUNK_SIZE);
	}

	// success
	*list_iter = iter;
	self->stats_chunk_get_dt += a3d_timestamp() - t0;
	return chunk;

	// failure
	fail_add:
		a3d_list_remove(self->chunks, &iter);
	fail_append:
	{
		int dsize;
		osmdb_chunk_delete(&chunk, &dsize);
		self->err = 1;
	}
	self->stats_chunk_get_dt += a3d_timestamp() - t0;
	return NULL;
}

static osmdb_tile_t*
osmdb_index_getTile(osmdb_index_t* self,
                    int zoom, int x, int y,
                    const char* key,
                    a3d_listitem_t** list_iter)
{
	assert(self);
	assert(key);
	assert(list_iter);

	double t0 = a3d_timestamp();
	self->stats_tile_get += 1.0;

	// check if tile is already in hash
	a3d_hashmapIter_t hiter;
	osmdb_tile_t*     tile;
	a3d_listitem_t*   iter;
	iter = (a3d_listitem_t*)
	       a3d_hashmap_find(self->hash_tiles, &hiter, key);
	if(iter)
	{
		self->stats_tile_hit += 1.0;
		tile = (osmdb_tile_t*) a3d_list_peekitem(iter);
		a3d_list_moven(self->tiles, iter, NULL);
	}
	else
	{
		self->stats_tile_miss += 1.0;

		// import the tile if it exists
		char fname[256];
		osmdb_tile_fname(self->base, zoom, x, y, fname);
		int    exists  = osmdb_fileExists(fname);
		double load_t0 = a3d_timestamp();
		if(exists)
		{
			self->stats_tile_load += 1.0;
		}

		tile = osmdb_tile_new(zoom, x, y,
		                      self->base, exists);
		if(tile == NULL)
		{
			self->err = 1;
			self->stats_tile_get_dt += a3d_timestamp() - t0;
			return NULL;
		}

		if(exists)
		{
			self->stats_tile_load_dt += a3d_timestamp() - load_t0;
		}

		iter = a3d_list_append(self->tiles,
		                       NULL, (const void*) tile);
		if(iter == NULL)
		{
			goto fail_append;
		}

		if(a3d_hashmap_add(self->hash_tiles,
		                   (const void*) iter, key) == 0)
		{
			goto fail_add;
		}
		self->size_tiles += osmdb_tile_size(tile);
		osmdb_index_trimTiles(self, OSMDB_TILE_SIZE);
	}

	// success
	*list_iter = iter;
	self->stats_tile_get_dt += a3d_timestamp() - t0;
	return tile;

	// failure
	fail_add:
		a3d_list_remove(self->tiles, &iter);
	fail_append:
	{
		osmdb_tile_delete(&tile);
		self->err = 1;
	}
	self->stats_tile_get_dt += a3d_timestamp() - t0;
	return NULL;
}

/***********************************************************
* public - osmdb_indexIter_t                               *
***********************************************************/

osmdb_indexIter_t*
osmdb_indexIter_new(struct osmdb_index_s* index, int type)
{
	assert(index);

	if(osmdb_index_flushChunks(index, type) == 0)
	{
		return NULL;
	}

	osmdb_indexIter_t* self = (osmdb_indexIter_t*)
	                          malloc(sizeof(osmdb_indexIter_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
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
		free(self);
	return NULL;
}

void osmdb_indexIter_delete(osmdb_indexIter_t** _self)
{
	assert(_self);

	osmdb_indexIter_t* self = *_self;
	if(self)
	{
		closedir(self->dir);
		free(self);
		*_self = NULL;
	}
}

osmdb_indexIter_t* osmdb_indexIter_next(osmdb_indexIter_t* self)
{
	assert(self);

	// get the next item in the chunk
	if(self->chunk_iter)
	{
		self->chunk_iter = a3d_hashmap_next(self->chunk_iter);
		if(self->chunk_iter)
		{
			a3d_list_moven(self->index->chunks, self->list_iter, NULL);
			return self;
		}
		else
		{
			osmdb_chunk_t* chunk = (osmdb_chunk_t*)
			                       a3d_list_peekitem(self->list_iter);
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

				// get hash and key
				char key[256];
				a3d_hashmap_t* hash = osmdb_index_getHash(self->index,
				                                          self->type);
				if(hash == NULL)
				{
					self->chunk_iter = NULL;
					self->de = readdir(self->dir);
					return osmdb_indexIter_next(self);
				}
				snprintf(key, 256, "%0.0lf", idu);

				// get the chunk
				a3d_listitem_t* list_iter;
				osmdb_chunk_t*  chunk;
				chunk = osmdb_index_getChunk(self->index, hash,
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
				self->chunk_iter = a3d_hashmap_head(chunk->hash,
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
	assert(self);

	if(self->chunk_iter == NULL)
	{
		return NULL;
	}

	return a3d_hashmap_val(self->chunk_iter);
}

static int
osmdb_index_gatherNode(osmdb_index_t* self,
                       xml_ostream_t* os, double id,
                       a3d_hashmap_t* hash_nodes)
{
	assert(self);
	assert(os);
	assert(hash_nodes);

	// check if id already included
	a3d_hashmapIter_t iter;
	if(a3d_hashmap_findf(hash_nodes, &iter, "%0.0lf", id))
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
	if(a3d_hashmap_addf(hash_nodes,
	                    (const void*) &OSMDB_INDEX_ONE,
	                    "%0.0lf", id) == 0)
	{
		return 0;
	}

	return osmdb_node_export(node, os);
}

static int
osmdb_index_gatherWay(osmdb_index_t* self,
                      xml_ostream_t* os,
                      double id,
                      a3d_hashmap_t* hash_nodes,
                      a3d_hashmap_t* hash_ways)
{
	assert(self);
	assert(os);
	assert(hash_nodes);
	assert(hash_ways);

	// check if id already included
	a3d_hashmapIter_t iterator;
	if(a3d_hashmap_findf(hash_ways, &iterator,
	                     "%0.0lf", id))
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

	// gather nds
	a3d_listitem_t* iter = a3d_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_peekitem(iter);
		if(osmdb_index_gatherNode(self, os, *ref,
		                          hash_nodes) == 0)
		{
			return 0;
		}
		iter = a3d_list_next(iter);
	}

	// mark the way as found
	if(a3d_hashmap_addf(hash_ways,
	                    (const void*) &OSMDB_INDEX_ONE,
	                    "%0.0lf", id) == 0)
	{
		return 0;
	}

	return osmdb_way_export(way, os);
}

static int
osmdb_index_fetchWay(osmdb_index_t* self,
                     xml_ostream_t* os,
                     double id,
                     a3d_hashmap_t* hash_nodes,
                     a3d_hashmap_t* hash_ways,
                     a3d_hashmap_t* hash_ways_join,
                     a3d_multimap_t* mm_nds_join)
{
	assert(self);
	assert(os);
	assert(hash_nodes);
	assert(hash_ways);
	assert(hash_ways_join);
	assert(mm_nds_join);

	// check if id already included
	a3d_hashmapIter_t iterator;
	if(a3d_hashmap_findf(hash_ways, &iterator,
	                     "%0.0lf", id))
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

	// mark the way as found
	if(a3d_hashmap_addf(hash_ways,
	                    (const void*) &OSMDB_INDEX_ONE,
	                    "%0.0lf", id) == 0)
	{
		return 0;
	}

	if(osmdb_index_addJoin(way, hash_ways_join,
	                       mm_nds_join) == 0)
	{
		return 0;
	}

	return 1;
}

static int
osmdb_index_joinWays(a3d_hashmap_t* hash_ways_join,
                     a3d_multimap_t* mm_nds_join)
{
	assert(hash_ways_join);
	assert(mm_nds_join);

	osmdb_way_t*        way1;
	osmdb_way_t*        way2;
	a3d_multimapIter_t  miterator1;
	a3d_multimapIter_t* miter1;
	a3d_multimapIter_t  miterator2;
	a3d_hashmapIter_t   hiter1;
	a3d_hashmapIter_t   hiterator2;
	a3d_hashmapIter_t*  hiter2;
	a3d_listitem_t*     iter1;
	a3d_listitem_t*     iter2;
	a3d_list_t*         list1;
	a3d_list_t*         list2;
	double*             id1;
	double*             id2;
	double              ref1;
	double              ref2;
	miter1 = a3d_multimap_head(mm_nds_join, &miterator1);
	while(miter1)
	{
		ref1  = strtod(a3d_multimap_key(miter1), NULL);
		list1 = (a3d_list_t*) a3d_multimap_list(miter1);
		iter1 = a3d_list_head(list1);
		while(iter1)
		{
			id1  = (double*) a3d_list_peekitem(iter1);
			if(*id1 == -1.0)
			{
				iter1 = a3d_list_next(iter1);
				continue;
			}

			way1 = (osmdb_way_t*)
			       a3d_hashmap_findf(hash_ways_join,
                                     &hiter1,
                                     "%0.0lf", *id1);
			if(way1 == NULL)
			{
				iter1 = a3d_list_next(iter1);
				continue;
			}

			iter2 = a3d_list_next(iter1);
			while(iter2)
			{
				hiter2 = &hiterator2;
				id2 = (double*)
				      a3d_list_peekitem(iter2);
				if(*id2 == -1.0)
				{
					iter2 = a3d_list_next(iter2);
					continue;
				}

				way2 = (osmdb_way_t*)
				       a3d_hashmap_findf(hash_ways_join,
				                         hiter2,
				                         "%0.0lf", *id2);
				if(way2 == NULL)
				{
					iter2 = a3d_list_next(iter2);
					continue;
				}

				if(osmdb_way_join(way1, way2, ref1, &ref2) == 0)
				{
					iter2 = a3d_list_next(iter2);
					continue;
				}

				// replace ref2->id2 with ref2->id1 in mm_nds_join
				list2 = (a3d_list_t*)
				        a3d_multimap_findf(mm_nds_join, &miterator2,
				                           "%0.0lf", ref2);
				iter2 = a3d_list_head(list2);
				while(iter2)
				{
					double* idx = (double*)
					              a3d_list_peekitem(iter2);
					if(*idx == *id2)
					{
						*idx = *id1;
						break;
					}

					iter2 = a3d_list_next(iter2);
				}

				// remove ways from mm_nds_join
				*id1 = -1.0;
				*id2 = -1.0;

				// remove way2 from hash_ways_join
				a3d_hashmap_remove(hash_ways_join, &hiter2);
				osmdb_way_delete(&way2);
				iter2 = NULL;
			}

			iter1 = a3d_list_next(iter1);
		}

		miter1 = a3d_multimap_nextList(miter1);
	}

	return 1;
}

static int
osmdb_index_exportWays(osmdb_index_t* self,
                       xml_ostream_t* os,
                       a3d_hashmap_t* hash_ways_join,
                       a3d_hashmap_t* hash_nodes)
{
	assert(self);
	assert(os);
	assert(hash_ways_join);
	assert(hash_nodes);

	a3d_hashmapIter_t  hiterator;
	a3d_hashmapIter_t* hiter;
	hiter = a3d_hashmap_head(hash_ways_join, &hiterator);
	while(hiter)
	{
		osmdb_way_t* way;
		way = (osmdb_way_t*)
		      a3d_hashmap_val(hiter);

		// gather nds
		a3d_listitem_t* iter = a3d_list_head(way->nds);
		while(iter)
		{
			double* ref = (double*)
			              a3d_list_peekitem(iter);
			if(osmdb_index_gatherNode(self, os, *ref,
			                          hash_nodes) == 0)
			{
				return 0;
			}
			iter = a3d_list_next(iter);
		}

		if(osmdb_way_export(way, os) == 0)
		{
			return 0;
		}

		hiter = a3d_hashmap_next(hiter);
	}

	return 1;
}

static int
osmdb_index_gatherRelation(osmdb_index_t* self,
                           xml_ostream_t* os,
                           double id, int zoom,
                           a3d_hashmap_t* hash_nodes,
                           a3d_hashmap_t* hash_ways,
                           a3d_hashmap_t* hash_relations)
{
	assert(self);
	assert(os);
	assert(hash_nodes);
	assert(hash_ways);
	assert(hash_relations);

	// check if id already included
	a3d_hashmapIter_t iterator;
	if(a3d_hashmap_findf(hash_relations, &iterator,
	                     "%0.0lf", id))
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
	a3d_listitem_t* iter = a3d_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		              a3d_list_peekitem(iter);
		if(m->type == OSMDB_TYPE_NODE)
		{
			if(osmdb_index_gatherNode(self, os, m->ref,
			                          hash_nodes) == 0)
			{
				return 0;
			}
		}
		else if(m->type == OSMDB_TYPE_WAY)
		{
			if(osmdb_index_gatherWay(self, os, m->ref,
			                         hash_nodes,
			                         hash_ways) == 0)
			{
				return 0;
			}
		}
		iter = a3d_list_next(iter);
	}

	// mark the relation as found
	if(a3d_hashmap_addf(hash_relations,
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
                       int zoom,
                       a3d_hashmap_t* hash_nodes,
                       a3d_hashmap_t* hash_ways,
                       a3d_hashmap_t* hash_relations,
                       a3d_hashmap_t* hash_ways_join,
                       a3d_multimap_t* mm_nds_join)
{
	assert(self);
	assert(os);
	assert(tile);
	assert(hash_nodes);
	assert(hash_ways);
	assert(hash_relations);
	assert(hash_ways_join);
	assert(mm_nds_join);

	a3d_hashmapIter_t  iterator;
	a3d_hashmapIter_t* iter;
	iter = a3d_hashmap_head(tile->hash_nodes, &iterator);
	while(iter)
	{
		double ref = strtod(a3d_hashmap_key(iter), NULL);

		if(osmdb_index_gatherNode(self, os, ref,
		                          hash_nodes) == 0)
		{
			return 0;
		}

		iter = a3d_hashmap_next(iter);
	}

	iter = a3d_hashmap_head(tile->hash_relations, &iterator);
	while(iter)
	{
		double ref = strtod(a3d_hashmap_key(iter), NULL);

		if(osmdb_index_gatherRelation(self, os, ref, zoom,
		                              hash_nodes,
		                              hash_ways,
		                              hash_relations) == 0)
		{
			return 0;
		}

		iter = a3d_hashmap_next(iter);
	}

	iter = a3d_hashmap_head(tile->hash_ways, &iterator);
	while(iter)
	{
		double ref = strtod(a3d_hashmap_key(iter), NULL);

		if(osmdb_index_fetchWay(self, os, ref,
		                        hash_nodes, hash_ways,
		                        hash_ways_join,
		                        mm_nds_join) == 0)
		{
			return 0;
		}

		iter = a3d_hashmap_next(iter);
	}

	if(osmdb_index_joinWays(hash_ways_join,
	                        mm_nds_join) == 0)
	{
		return 0;
	}

	if(osmdb_index_exportWays(self, os, hash_ways_join,
	                          hash_nodes) == 0)
	{
		return 0;
	}

	return 1;
}

static int osmdb_index_addTileXY(osmdb_index_t* self,
                                 int zoom, int x, int y,
                                 int type, double id)
{
	assert(self);

	double t0 = a3d_timestamp();
	self->stats_tile_add += 1.0;

	char key[256];
	snprintf(key, 256, "Z%iX%iY%i", zoom, x, y);

	// get the tile
	a3d_listitem_t* list_iter;
	osmdb_tile_t* tile;
	tile = osmdb_index_getTile(self, zoom, x, y,
	                           key, &list_iter);
	if(tile == NULL)
	{
		// nothing to undo for this failure
		LOGE("invalid key=%s", key);
		self->err = 1;
		self->stats_tile_add_dt += a3d_timestamp() - t0;
		return 0;
	}

	// find the data
	if(osmdb_tile_find(tile, type, id))
	{
		self->stats_tile_add_dt += a3d_timestamp() - t0;
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
		self->stats_tile_add_dt += a3d_timestamp() - t0;
		return 0;
	}
	int tsz1 = osmdb_tile_size(tile);
	self->size_tiles += tsz1 - tsz0;
	osmdb_index_trimTiles(self, OSMDB_TILE_SIZE);

	self->stats_tile_add_dt += a3d_timestamp() - t0;
	return 1;
}

static int osmdb_index_addTile(osmdb_index_t* self,
                               osmdb_range_t* range,
                               int zoom,
                               int type, double id)
 {
	assert(self);
	assert(range);

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

static int
osmdb_index_sampleWay(osmdb_index_t* self,
                      osmdb_way_t* way,
                      int center,
                      osmdb_range_t* range,
                      osmdb_way_t** _way8,
                      osmdb_way_t** _way11,
                      osmdb_way_t** _way14)
{
	assert(self);
	assert(way);
	assert(range);
	assert(_way8);
	assert(_way11);
	assert(_way14);

	osmdb_way_t* way8 = osmdb_way_copyEmpty(way);
	if(way8 == NULL)
	{
		return 0;
	}

	osmdb_way_t* way11 = osmdb_way_copyEmpty(way);
	if(way11 == NULL)
	{
		goto fail_way11;
	}

	osmdb_way_t* way14 = osmdb_way_copyEmpty(way);
	if(way14 == NULL)
	{
		goto fail_way14;
	}

	osmdb_range_init(range);

	int first = 1;
	int last  = 0;
	a3d_vec2f_t p0_8  = { .x = 0.0f, .y=0.0f };
	a3d_vec2f_t p0_11 = { .x = 0.0f, .y=0.0f };
	a3d_vec2f_t p0_14 = { .x = 0.0f, .y=0.0f };
	a3d_listitem_t* iter = a3d_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_peekitem(iter);

		osmdb_node_t* node;
		node = (osmdb_node_t*)
		       osmdb_index_find(self, OSMDB_TYPE_NODE, *ref);
		if(node == NULL)
		{
			iter = a3d_list_next(iter);
			continue;
		}

		osmdb_range_addPt(range, node->lat, node->lon);

		// skip sampling ways when centering
		if(center)
		{
			iter = a3d_list_next(iter);
			continue;
		}

		// set the last flag
		if(a3d_list_next(iter) == NULL)
		{
			last = 1;
		}

		// compute distance between previous point
		a3d_vec2f_t p1_8;
		a3d_vec2f_t p1_11;
		a3d_vec2f_t p1_14;
		terrain_coord2xy(node->lat, node->lon, &p1_8.x,  &p1_8.y);
		terrain_coord2xy(node->lat, node->lon, &p1_11.x, &p1_11.y);
		terrain_coord2xy(node->lat, node->lon, &p1_14.x, &p1_14.y);
		float dist8  = a3d_vec2f_distance(&p1_8,  &p0_8);
		float dist11 = a3d_vec2f_distance(&p1_11, &p0_11);
		float dist14 = a3d_vec2f_distance(&p1_14, &p0_14);

		// add the sampled points to the corresponding ways
		if(first || last || (dist8 >= self->min_dist8))
		{
			if(osmdb_way_ref(way8, *ref) == 0)
			{
				goto fail_ref;
			}

			a3d_vec2f_copy(&p1_8, &p0_8);
			self->stats_sample_way8 += 1.0;
		}

		if(first || last || (dist11 >= self->min_dist11))
		{
			if(osmdb_way_ref(way11, *ref) == 0)
			{
				goto fail_ref;
			}

			a3d_vec2f_copy(&p1_11, &p0_11);
			self->stats_sample_way11 += 1.0;
		}

		if(first || last || (dist14 >= self->min_dist14))
		{
			if(osmdb_way_ref(way14, *ref) == 0)
			{
				goto fail_ref;
			}

			a3d_vec2f_copy(&p1_14, &p0_14);
			self->stats_sample_way14 += 1.0;
		}

		first = 0;
		self->stats_sample_ways += 1.0;
		iter = a3d_list_next(iter);
	}

	// update ways
	osmdb_way_updateRange(way8, range);
	osmdb_way_updateRange(way11, range);
	osmdb_way_updateRange(way14, range);
	*_way8  = way8;
	*_way11 = way11;
	*_way14 = way14;

	// success
	return 1;

	// failure
	fail_ref:
		osmdb_way_delete(&way14);
	fail_way14:
		osmdb_way_delete(&way11);
	fail_way11:
		osmdb_way_delete(&way8);
	return 0;
}

static void
osmdb_index_rangeWay(osmdb_index_t* self,
                     osmdb_way_t* way,
                     int center,
                     osmdb_range_t* range)
{
	assert(self);
	assert(way);
	assert(range);

	osmdb_range_init(range);

	a3d_listitem_t* iter = a3d_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_peekitem(iter);

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
				iter = a3d_list_next(iter);
				continue;
			}
		}

		osmdb_range_addPt(range, node->lat, node->lon);

		iter = a3d_list_next(iter);
	}
}

static void
osmdb_index_rangeRelation(osmdb_index_t* self,
                          osmdb_relation_t* relation,
                          int center,
                          osmdb_range_t* range)
{
	assert(self);
	assert(relation);
	assert(range);

	osmdb_range_init(range);

	a3d_listitem_t* iter = a3d_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    a3d_list_peekitem(iter);

		if(m->type == OSMDB_TYPE_WAY)
		{
			osmdb_way_t* way;
			way = (osmdb_way_t*)
			       osmdb_index_find(self,
			                        OSMDB_TYPE_WAY,
			                        m->ref);
			if(way == NULL)
			{
				iter = a3d_list_next(iter);
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
					iter = a3d_list_next(iter);
					continue;
				}
			}

			osmdb_range_init(range);
			osmdb_range_addPt(range, node->lat, node->lon);
			return;
		}

		iter = a3d_list_next(iter);
	}
}

/***********************************************************
* public - osmdb_index_t                                   *
***********************************************************/

osmdb_index_t* osmdb_index_new(const char* base)
{
	assert(base);

	osmdb_index_t* self = (osmdb_index_t*)
	                      malloc(sizeof(osmdb_index_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->chunks = a3d_list_new();
	if(self->chunks == NULL)
	{
		goto fail_chunks;
	}

	self->hash_nodes = a3d_hashmap_new();
	if(self->hash_nodes == NULL)
	{
		goto fail_nodes;
	}

	self->hash_ways = a3d_hashmap_new();
	if(self->hash_ways == NULL)
	{
		goto fail_ways;
	}

	self->hash_relations = a3d_hashmap_new();
	if(self->hash_relations == NULL)
	{
		goto fail_relations;
	}

	self->hash_ctrnodes = a3d_hashmap_new();
	if(self->hash_ctrnodes == NULL)
	{
		goto fail_ctrnodes;
	}

	self->hash_noderefs = a3d_hashmap_new();
	if(self->hash_noderefs == NULL)
	{
		goto fail_noderefs;
	}

	self->hash_wayrefs = a3d_hashmap_new();
	if(self->hash_wayrefs == NULL)
	{
		goto fail_wayrefs;
	}

	self->hash_ctrnoderefs = a3d_hashmap_new();
	if(self->hash_ctrnoderefs == NULL)
	{
		goto fail_ctrnoderefs;
	}

	self->hash_ctrwayrefs = a3d_hashmap_new();
	if(self->hash_ctrwayrefs == NULL)
	{
		goto fail_ctrwayrefs;
	}

	self->tiles = a3d_list_new();
	if(self->tiles == NULL)
	{
		goto fail_tiles;
	}

	self->hash_tiles = a3d_hashmap_new();
	if(self->hash_tiles == NULL)
	{
		goto fail_hash_tiles;
	}

	snprintf(self->base, 256, "%s", base);
	self->size_chunks         = 0;
	self->size_hash           = 0;
	self->size_tiles          = 0;
	self->err                 = 0;
	self->stats_chunk_hit     = 0.0;
	self->stats_chunk_miss    = 0.0;
	self->stats_chunk_evict   = 0.0;
	self->stats_chunk_add     = 0.0;
	self->stats_chunk_add_dt  = 0.0;
	self->stats_chunk_find    = 0.0;
	self->stats_chunk_find_dt = 0.0;
	self->stats_chunk_get     = 0.0;
	self->stats_chunk_get_dt  = 0.0;
	self->stats_chunk_load    = 0.0;
	self->stats_chunk_load_dt = 0.0;
	self->stats_chunk_trim    = 0.0;
	self->stats_chunk_trim_dt = 0.0;
	self->stats_tile_hit      = 0.0;
	self->stats_tile_miss     = 0.0;
	self->stats_tile_evict    = 0.0;
	self->stats_tile_add      = 0.0;
	self->stats_tile_add_dt   = 0.0;
	self->stats_tile_make     = 0.0;
	self->stats_tile_make_dt  = 0.0;
	self->stats_tile_get      = 0.0;
	self->stats_tile_get_dt   = 0.0;
	self->stats_tile_load     = 0.0;
	self->stats_tile_load_dt  = 0.0;
	self->stats_tile_trim     = 0.0;
	self->stats_tile_trim_dt  = 0.0;
	self->stats_sample_way8   = 0.0;
	self->stats_sample_way11  = 0.0;
	self->stats_sample_way14  = 0.0;
	self->stats_sample_ways   = 0.0;

	osmdb_index_computeMinDist(self);

	// success
	return self;

	// failure
	fail_hash_tiles:
		a3d_list_delete(&self->tiles);
	fail_tiles:
		a3d_hashmap_delete(&self->hash_ctrwayrefs);
	fail_ctrwayrefs:
		a3d_hashmap_delete(&self->hash_ctrnoderefs);
	fail_ctrnoderefs:
		a3d_hashmap_delete(&self->hash_wayrefs);
	fail_wayrefs:
		a3d_hashmap_delete(&self->hash_noderefs);
	fail_noderefs:
		a3d_hashmap_delete(&self->hash_ctrnodes);
	fail_ctrnodes:
		a3d_hashmap_delete(&self->hash_relations);
	fail_relations:
		a3d_hashmap_delete(&self->hash_ways);
	fail_ways:
		a3d_hashmap_delete(&self->hash_nodes);
	fail_nodes:
		a3d_list_delete(&self->chunks);
	fail_chunks:
		free(self);
	return NULL;
}

int osmdb_index_delete(osmdb_index_t** _self)
{
	assert(_self);

	int err = 0;

	osmdb_index_t* self = *_self;
	if(self)
	{
		osmdb_index_trimChunks(self, 0);
		osmdb_index_trimTiles(self, 0);
		a3d_hashmap_delete(&self->hash_tiles);
		a3d_list_delete(&self->tiles);
		a3d_hashmap_delete(&self->hash_ctrwayrefs);
		a3d_hashmap_delete(&self->hash_ctrnoderefs);
		a3d_hashmap_delete(&self->hash_wayrefs);
		a3d_hashmap_delete(&self->hash_noderefs);
		a3d_hashmap_delete(&self->hash_ctrnodes);
		a3d_hashmap_delete(&self->hash_relations);
		a3d_hashmap_delete(&self->hash_ways);
		a3d_hashmap_delete(&self->hash_nodes);
		a3d_list_delete(&self->chunks);
		err = self->err;
		osmdb_index_stats(self);
		free(self);
		*_self = NULL;
	}

	return err ? 0 : 1;
}

int osmdb_index_error(osmdb_index_t* self)
{
	assert(self);

	return self->err;
}

int osmdb_index_addChunk(osmdb_index_t* self,
                         int type, const void* data)
{
	assert(self);
	assert(data);

	double t0 = a3d_timestamp();
	self->stats_chunk_add += 1.0;

	// get data/hash attributes
	int    dsize;
	double id;
	double idu = 0.0;
	double idl = 0.0;
	char   key[256];
	a3d_hashmap_t*    hash;
	osmdb_node_t*     node     = NULL;
	osmdb_way_t*      way      = NULL;
	osmdb_relation_t* relation = NULL;
	double*           ref      = NULL;
	if(type == OSMDB_TYPE_NODE)
	{
		node  = (osmdb_node_t*) data;
		hash  = self->hash_nodes;
		id    = node->id;
		dsize = osmdb_node_size(node);
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		way   = (osmdb_way_t*) data;
		hash  = self->hash_ways;
		id    = way->id;
		dsize = osmdb_way_size(way);
	}
	else if(type == OSMDB_TYPE_RELATION)
	{
		relation = (osmdb_relation_t*) data;
		hash     = self->hash_relations;
		id       = relation->id;
		dsize    = osmdb_relation_size(relation);
	}
	else if(type == OSMDB_TYPE_CTRNODE)
	{
		node = (osmdb_node_t*) data;
		hash = self->hash_ctrnodes;
		id   = node->id;
		dsize = osmdb_node_size(node);
	}
	else if(type == OSMDB_TYPE_NODEREF)
	{
		ref   = (double*) data;
		hash  = self->hash_noderefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else if(type == OSMDB_TYPE_WAYREF)
	{
		ref   = (double*) data;
		hash  = self->hash_wayrefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else if(type == OSMDB_TYPE_CTRNODEREF)
	{
		ref   = (double*) data;
		hash  = self->hash_ctrnoderefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else if(type == OSMDB_TYPE_CTRWAYREF)
	{
		ref   = (double*) data;
		hash  = self->hash_ctrwayrefs;
		id    = *ref;
		dsize = (int) sizeof(double);
	}
	else
	{
		self->stats_chunk_add_dt += a3d_timestamp() - t0;
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
			free(ref);
		}
		else
		{
			self->stats_chunk_add_dt += a3d_timestamp() - t0;
			LOGE("invalid type=%i", type);
			return 0;
		}

		self->stats_chunk_add_dt += a3d_timestamp() - t0;
		return 1;
	}

	// get the chunk
	a3d_listitem_t* list_iter;
	osmdb_chunk_t* chunk;
	chunk = osmdb_index_getChunk(self, hash,
	                             key, idu, type, 0,
	                             &list_iter);
	if(chunk == NULL)
	{
		// nothing to undo for this failure
		LOGE("invalid id=%0.0lf, idu=%0.0lf, idl=%0.0lf",
		     id, idu, idl);
		self->err = 1;
		self->stats_chunk_add_dt += a3d_timestamp() - t0;
		return 0;
	}

	// add the data
	if(osmdb_chunk_add(chunk, data, idl, dsize) == 0)
	{
		// nothing to undo for this failure
		LOGE("failure key=%s, type=%i, id=%0.0lf, idu=%0.0lf, idl=%0.0lf",
		     key, type, id, idu, idl);
		self->err = 1;
		self->stats_chunk_add_dt += a3d_timestamp() - t0;
		return 0;
	}
	self->size_chunks += dsize;
	osmdb_index_trimChunks(self, OSMDB_CHUNK_SIZE);

	self->stats_chunk_add_dt += a3d_timestamp() - t0;
	return 1;
}

int osmdb_index_addNode(osmdb_index_t* self,
                        int zoom, int center,
                        int selected,
                        osmdb_node_t* node)
{
	assert(self);
	assert(node);

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
	assert(self);
	assert(way);

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
	assert(self);
	assert(relation);

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
	assert(self);
	assert(os);

	double t0 = a3d_timestamp();
	self->stats_tile_make += 1.0;

	char key[256];
	snprintf(key, 256, "Z%iX%iY%i", zoom, x, y);

	// get the tile
	a3d_listitem_t* list_iter;
	osmdb_tile_t* tile;
	tile = osmdb_index_getTile(self, zoom, x, y,
	                           key, &list_iter);
	if(tile == NULL)
	{
		// this error doesn't affect the consistency
		// of the database so don't set the err flag
		LOGE("invalid key=%s", key);
		self->stats_tile_make_dt += a3d_timestamp() - t0;
		return 0;
	}

	// allocate temporary hashes
	// which are needed to avoid duplicate entries
	// maps from ids to OSMDB_INDEX_ONE
	a3d_hashmap_t* hash_nodes = a3d_hashmap_new();
	if(hash_nodes == NULL)
	{
		goto fail_nodes;
	}

	a3d_hashmap_t* hash_ways = a3d_hashmap_new();
	if(hash_ways == NULL)
	{
		goto fail_ways;
	}

	a3d_hashmap_t* hash_relations = a3d_hashmap_new();
	if(hash_relations == NULL)
	{
		goto fail_relations;
	}

	a3d_hashmap_t* hash_ways_join = a3d_hashmap_new();
	if(hash_ways_join == NULL)
	{
		goto fail_ways_join;
	}

	a3d_multimap_t* mm_nds_join = a3d_multimap_new(NULL);
	if(mm_nds_join == NULL)
	{
		goto fail_nds_join;
	}

	// stream data
	int ret = xml_ostream_begin(os, "osmdb");
	if(osmdb_index_gatherTile(self, os, tile, zoom,
	                          hash_nodes, hash_ways,
	                          hash_relations,
	                          hash_ways_join,
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

	osmdb_index_discardJoin(hash_ways_join,
	                        mm_nds_join);
	a3d_hashmap_discard(hash_nodes);
	a3d_hashmap_discard(hash_ways);
	a3d_hashmap_discard(hash_relations);
	a3d_multimap_delete(&mm_nds_join);
	a3d_hashmap_delete(&hash_ways_join);
	a3d_hashmap_delete(&hash_relations);
	a3d_hashmap_delete(&hash_ways);
	a3d_hashmap_delete(&hash_nodes);

	self->stats_tile_make_dt += a3d_timestamp() - t0;

	// success
	return 1;

	// failure
	fail_os:
	fail_gather:
		osmdb_index_discardJoin(hash_ways_join,
		                        mm_nds_join);
		a3d_hashmap_discard(hash_relations);
		a3d_hashmap_discard(hash_ways);
		a3d_hashmap_discard(hash_nodes);
		a3d_multimap_delete(&mm_nds_join);
	fail_nds_join:
		a3d_hashmap_delete(&hash_ways_join);
	fail_ways_join:
		a3d_hashmap_delete(&hash_relations);
	fail_relations:
		a3d_hashmap_delete(&hash_ways);
	fail_ways:
		a3d_hashmap_delete(&hash_nodes);
	fail_nodes:
		self->stats_tile_make_dt += a3d_timestamp() - t0;
	return 0;
}

const void* osmdb_index_find(osmdb_index_t* self,
                             int type, double id)
{
	assert(self);

	double t0 = a3d_timestamp();
	self->stats_chunk_find += 1.0;

	// get hash attributes
	double idu;
	double idl;
	char   key[256];
	a3d_hashmap_t* hash = osmdb_index_getHash(self, type);
	if(hash == NULL)
	{
		self->stats_chunk_find_dt += a3d_timestamp() - t0;
		return NULL;
	}
	osmdb_splitId(id, &idu, &idl);
	snprintf(key, 256, "%0.0lf", idu);

	// get the chunk
	a3d_listitem_t* list_iter;
	osmdb_chunk_t* chunk;
	chunk = osmdb_index_getChunk(self, hash,
	                             key, idu, type, 1,
	                             &list_iter);
	if(chunk == NULL)
	{
		// data not found or an error occurred in getChunk
		// don't set the err flag here
		self->stats_chunk_find_dt += a3d_timestamp() - t0;
		return NULL;
	}

	// find the data
	const void* data = osmdb_chunk_find(chunk, idl);
	self->stats_chunk_find_dt += a3d_timestamp() - t0;
	return data;
}

void osmdb_index_stats(osmdb_index_t* self)
{
	assert(self);

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
	LOGI("WAY8/11/14: %0.0lf/%0.0lf/%0.0lf of %0.0lf",
	     self->stats_sample_way8,
	     self->stats_sample_way11,
	     self->stats_sample_way14,
	     self->stats_sample_ways);
}

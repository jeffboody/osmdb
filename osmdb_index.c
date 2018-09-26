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
#include "a3d/a3d_timestamp.h"
#include "osmdb_chunk.h"
#include "osmdb_tile.h"
#include "osmdb_index.h"
#include "osmdb_node.h"
#include "osmdb_way.h"
#include "osmdb_relation.h"
#include "osmdb_util.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

#define OSMDB_CHUNK_SIZE 250*1024*1024
#define OSMDB_TILE_SIZE  2500000

/***********************************************************
* private                                                  *
***********************************************************/

static void
osmdb_index_trimChunks(osmdb_index_t* self, int size_chunks)
{
	assert(self);
	assert(size_chunks >= 0);

	a3d_listitem_t* item = a3d_list_head(self->chunks);
	while(item)
	{
		if(self->size_chunks <= size_chunks)
		{
			return;
		}

		double t0 = a3d_timestamp();
		self->stats_chunk_trim += 1.0;
		if(size_chunks > 0)
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
		a3d_hashmap_t* hash;
		if(chunk->type == OSMDB_TYPE_NODE)
		{
			hash = self->hash_nodes;
		}
		else if(chunk->type == OSMDB_TYPE_WAY)
		{
			hash = self->hash_ways;
		}
		else
		{
			hash = self->hash_relations;
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
		a3d_hashmap_remove(hash, &iter);
		a3d_list_remove(self->chunks, &item);

		// delete the chunk
		int dsize = 0;
		if(osmdb_chunk_delete(&chunk, &dsize) == 0)
		{
			self->err = 1;
		}
		self->size_chunks -= dsize;
		self->stats_chunk_trim_dt += a3d_timestamp() - t0;
	}
}

static void
osmdb_index_trimTiles(osmdb_index_t* self, int size_tiles)
{
	assert(self);
	assert(size_tiles >= 0);

	a3d_listitem_t* item = a3d_list_head(self->tiles);
	while(item)
	{
		if(self->size_tiles <= size_tiles)
		{
			return;
		}

		double t0 = a3d_timestamp();
		self->stats_tile_trim += 1.0;
		if(size_tiles > 0)
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
		int dsize = 0;
		if(osmdb_tile_delete(&tile, &dsize) == 0)
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

		if(a3d_hashmap_add(hash, &hiter,
		                   (const void*) iter, key) == 0)
		{
			goto fail_add;
		}
		self->size_chunks += csize;
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

		int tsize;
		tile = osmdb_tile_new(zoom, x, y, self->base,
		                      exists, &tsize);
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

		if(a3d_hashmap_add(self->hash_tiles, &hiter,
		                   (const void*) iter, key) == 0)
		{
			goto fail_add;
		}
		self->size_tiles += tsize;
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
		int dsize;
		osmdb_tile_delete(&tile, &dsize);
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

	osmdb_indexIter_t* self = (osmdb_indexIter_t*)
	                          malloc(sizeof(osmdb_indexIter_t));
	if(self == NULL)
	{
		return NULL;
	}

	char path[256];
	if(type == OSMDB_TYPE_NODE)
	{
		snprintf(path, 256, "%s/node/", index->base);
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		snprintf(path, 256, "%s/way/", index->base);
	}
	else
	{
		snprintf(path, 256, "%s/relation/", index->base);
	}

	self->dir = opendir(path);
	if(self->dir == NULL)
	{
		LOGE("invalid path=%s", path);
		goto fail_dir;
	}

	self->de         = readdir(self->dir);
	self->index      = index;
	self->type       = type;
	self->chunk_iter = NULL;

	// success
	return osmdb_indexIter_next(self);

	// failure
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

			char* ext = strstr(name, ".xmlz");
			if(ext)
			{
				// extract idu from name
				ext[0] = '\0';
				double idu = strtod(name, NULL);

				// get hash and key
				char key[256];
				a3d_hashmap_t* hash;
				if(self->type == OSMDB_TYPE_NODE)
				{
					hash = self->index->hash_nodes;
				}
				else if(self->type == OSMDB_TYPE_WAY)
				{
					hash = self->index->hash_ways;
				}
				else
				{
					hash = self->index->hash_relations;
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
				self->chunk_iter = a3d_hashmap_head(chunk->hash, &self->chunk_iterator);
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
	self->stats_chunk_trim    = 0.0;
	self->stats_chunk_trim_dt = 0.0;
	self->stats_tile_hit      = 0.0;
	self->stats_tile_miss     = 0.0;
	self->stats_tile_evict    = 0.0;
	self->stats_tile_add      = 0.0;
	self->stats_tile_add_dt   = 0.0;
	self->stats_tile_get      = 0.0;
	self->stats_tile_get_dt   = 0.0;
	self->stats_tile_trim     = 0.0;
	self->stats_tile_trim_dt  = 0.0;

	// success
	return self;

	// failure
	fail_hash_tiles:
		a3d_list_delete(&self->tiles);
	fail_tiles:
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
	if(type == OSMDB_TYPE_NODE)
	{
		node = (osmdb_node_t*) data;
		hash = self->hash_nodes;
		id   = node->id;
		dsize = osmdb_node_size(node);
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		way  = (osmdb_way_t*) data;
		hash = self->hash_ways;
		id   = way->id;
		dsize = osmdb_way_size(way);
	}
	else
	{
		relation = (osmdb_relation_t*) data;
		hash     = self->hash_relations;
		id       = relation->id;
		dsize = osmdb_relation_size(relation);
	}
	osmdb_splitId(id, &idu, &idl);
	snprintf(key, 256, "%0.0lf", idu);

	// check if the data already exists
	if(osmdb_index_find(self, type, id))
	{
		if(type == OSMDB_TYPE_NODE)
		{
			osmdb_node_delete(&node);
		}
		else if(type == OSMDB_TYPE_WAY)
		{
			osmdb_way_delete(&way);
		}
		else
		{
			osmdb_relation_delete(&relation);
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

int osmdb_index_addTile(osmdb_index_t* self,
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
	if(osmdb_tile_add(tile, type, id) == 0)
	{
		// nothing to undo for this failure
		LOGE("failure key=%s, type=%i, id=%0.0lf",
		     key, type, id);
		self->err = 1;
		self->stats_tile_add_dt += a3d_timestamp() - t0;
		return 0;
	}
	self->size_tiles += 1;
	osmdb_index_trimTiles(self, OSMDB_TILE_SIZE);

	self->stats_tile_add_dt += a3d_timestamp() - t0;
	return 1;
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
	a3d_hashmap_t* hash;
	if(type == OSMDB_TYPE_NODE)
	{
		hash = self->hash_nodes;
	}
	else if(type == OSMDB_TYPE_WAY)
	{
		hash = self->hash_ways;
	}
	else
	{
		hash = self->hash_relations;
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
	LOGI("GET:  cnt=%0.0lf, dt=%lf", self->stats_tile_get, self->stats_tile_get_dt);
	LOGI("LOAD: cnt=%0.0lf, dt=%lf", self->stats_tile_load, self->stats_tile_load_dt);
	LOGI("TRIM: cnt=%0.0lf, dt=%lf", self->stats_tile_trim, self->stats_tile_trim_dt);
}

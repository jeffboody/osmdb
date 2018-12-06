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

#ifndef osmdb_index_H
#define osmdb_index_H

#include <dirent.h>
#include "../a3d/a3d_list.h"
#include "../a3d/a3d_hashmap.h"
#include "../libxmlstream/xml_ostream.h"
#include "osmdb_range.h"

#define OSMDB_TYPE_NODE           1
#define OSMDB_TYPE_WAY            2
#define OSMDB_TYPE_RELATION       3
// below are only intended for indexer and tiler
#define OSMDB_TYPE_NODEREF        4
#define OSMDB_TYPE_WAYREF         5
#define OSMDB_TYPE_CTRNODE        6
#define OSMDB_TYPE_CTRWAY         7
#define OSMDB_TYPE_CTRRELATION    8
#define OSMDB_TYPE_CTRNODEREF     9
#define OSMDB_TYPE_CTRWAYREF      10
#define OSMDB_TYPE_CTRRELATIONREF 11

struct osmdb_index_s;

typedef struct
{
	struct osmdb_index_s* index;
	int type;
	DIR* dir;
	struct dirent* de;
	a3d_hashmapIter_t  chunk_iterator;
	a3d_hashmapIter_t* chunk_iter;
	a3d_listitem_t*    list_iter;
} osmdb_indexIter_t;

osmdb_indexIter_t* osmdb_indexIter_new(struct osmdb_index_s* index,
                                       int type);
void               osmdb_indexIter_delete(osmdb_indexIter_t** _self);
osmdb_indexIter_t* osmdb_indexIter_next(osmdb_indexIter_t* self);
const void*        osmdb_indexIter_peek(osmdb_indexIter_t* self);

typedef struct osmdb_index_s
{
	char base[256];
	int  size_chunks;
	int  size_hash;
	int  size_tiles;
	int  err;

	// LRU cache of chunks
	a3d_list_t* chunks;

	// map from <idu> to listitems
	a3d_hashmap_t* hash_nodes;
	a3d_hashmap_t* hash_ways;
	a3d_hashmap_t* hash_relations;
	// below are only intended for indexer and tiler
	a3d_hashmap_t* hash_noderefs;
	a3d_hashmap_t* hash_wayrefs;
	a3d_hashmap_t* hash_ctrnodes;
	a3d_hashmap_t* hash_ctrways;
	a3d_hashmap_t* hash_ctrrelations;
	a3d_hashmap_t* hash_ctrnoderefs;
	a3d_hashmap_t* hash_ctrwayrefs;
	a3d_hashmap_t* hash_ctrrelationrefs;

	// LRU cache of tiles
	a3d_list_t* tiles;

	// map from ZzoomXxYy to listitems
	a3d_hashmap_t* hash_tiles;

	// stats
	double stats_chunk_hit;
	double stats_chunk_miss;
	double stats_chunk_evict;
	double stats_chunk_add;
	double stats_chunk_add_dt;
	double stats_chunk_find;
	double stats_chunk_find_dt;
	double stats_chunk_get;
	double stats_chunk_get_dt;
	double stats_chunk_load;
	double stats_chunk_load_dt;
	double stats_chunk_trim;
	double stats_chunk_trim_dt;
	double stats_tile_hit;
	double stats_tile_miss;
	double stats_tile_evict;
	double stats_tile_add;
	double stats_tile_add_dt;
	double stats_tile_make;
	double stats_tile_make_dt;
	double stats_tile_get;
	double stats_tile_get_dt;
	double stats_tile_load;
	double stats_tile_load_dt;
	double stats_tile_trim;
	double stats_tile_trim_dt;
} osmdb_index_t;

osmdb_index_t*     osmdb_index_new(const char* base);
int                osmdb_index_delete(osmdb_index_t** _self);
int                osmdb_index_addChunk(osmdb_index_t* self,
                                        int type, const void* data);
int                osmdb_index_addTile(osmdb_index_t* self,
                                       osmdb_range_t* range,
                                       int zoom,
                                       int type, double id);
int                osmdb_index_makeTile(osmdb_index_t* self,
                                        int zoom, int x, int y,
                                        xml_ostream_t* os);
const void*        osmdb_index_find(osmdb_index_t* self,
                                    int type, double id);
void               osmdb_index_stats(osmdb_index_t* self);

#endif

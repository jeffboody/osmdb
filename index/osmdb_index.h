/*
 * Copyright (c) 2021 Jeff Boody
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

#ifndef osm_index_H
#define osm_index_H

#include <pthread.h>
#include <stdint.h>

#include "libcc/cc_list.h"
#include "libcc/cc_map.h"
#include "libsqlite3/sqlite3.h"
#include "osmdb_type.h"

#define OSMDB_INDEX_MODE_READONLY 0
#define OSMDB_INDEX_MODE_CREATE   1
#define OSMDB_INDEX_MODE_APPEND   2

typedef struct osmdb_entry_s osmdb_entry_t;

typedef struct
{
	int     type;
	int64_t id;
} osmdb_cacheLoading_t;

typedef struct
{
	int   mode;
	int   nth;
	int   batch_size;
	float smem;

	sqlite3* db;

	// sqlite3 statements
	sqlite3_stmt* stmt_begin;
	sqlite3_stmt* stmt_end;
	sqlite3_stmt* stmt_changeset;
	sqlite3_stmt* stmt_insert[OSMDB_TYPE_COUNT];

	// allow select across multiple threads
	// rows: nth
	// cols: OSMDB_TYPE_COUNT
	sqlite3_stmt** stmt_select;

	// sqlite3 indices
	int idx_insert_id[OSMDB_TYPE_COUNT];
	int idx_insert_blob[OSMDB_TYPE_COUNT];

	// allow select across multiple threads
	// rows: nth
	// cols: OSMDB_TYPE_COUNT
	int* idx_select_id;

	// entry cache
	pthread_mutex_t       cache_mutex;
	pthread_cond_t        cache_cond;
	cc_map_t*             cache_map;
	cc_list_t*            cache_list;
	int                   cache_readers;
	int                   cache_editor;
	int                   cache_loaders;
	osmdb_cacheLoading_t* cache_loading; // array of nth
} osmdb_index_t;

osmdb_index_t* osmdb_index_new(const char* fname,
                               int mode, int nth,
                               float smem);
void           osmdb_index_delete(osmdb_index_t** _self);
int64_t        osmdb_index_changeset(osmdb_index_t* self);
void           osmdb_index_lock(osmdb_index_t* self);
void           osmdb_index_unlock(osmdb_index_t* self);
int            osmdb_index_get(osmdb_index_t* self,
                               int tid,
                               int type,
                               int64_t id,
                               osmdb_handle_t** _hnd);
void           osmdb_index_put(osmdb_index_t* self,
                               osmdb_handle_t** _hnd);

#endif

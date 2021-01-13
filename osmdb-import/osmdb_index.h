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
#include "osmdb_blob.h"

typedef struct
{
	int batch_size;

	sqlite3* db;

	// sqlite3 statements
	sqlite3_stmt* stmt_begin;
	sqlite3_stmt* stmt_end;
	sqlite3_stmt* stmt_insert[OSMDB_BLOB_TYPE_COUNT];
	sqlite3_stmt* stmt_select[OSMDB_BLOB_TYPE_COUNT];

	// sqlite3 indices
	int idx_insert_id[OSMDB_BLOB_TYPE_COUNT];
	int idx_insert_blob[OSMDB_BLOB_TYPE_COUNT];
	int idx_select_id[OSMDB_BLOB_TYPE_COUNT];

	// entry cache
	pthread_mutex_t cache_mutex;
	cc_map_t*       cache_map;
	cc_list_t*      cache_list;
} osmdb_index_t;

osmdb_index_t* osmdb_index_new(const char* fname);
void           osmdb_index_delete(osmdb_index_t** _self);
int            osmdb_index_get(osmdb_index_t* self,
                               int type,
                               int64_t id,
                               osmdb_blob_t** _blob);
void           osmdb_index_put(osmdb_index_t* self,
                               osmdb_blob_t** _blob);

#endif

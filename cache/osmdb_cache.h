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

#ifndef osmdb_cache_H
#define osmdb_cache_H

#include <inttypes.h>

#include "libsqlite3/sqlite3.h"

#define OSMDB_CACHE_MODE_CREATE 0
#define OSMDB_CACHE_MODE_IMPORT 1

typedef int (*osmdb_cacheLoaded_fn)(void* priv,
                                    int size,
                                    const void* data);

typedef struct
{
	int mode;
	int nth;

	sqlite3* db;

	// sqlite3 statements
	int            batch_size;
	sqlite3_stmt*  stmt_begin;
	sqlite3_stmt*  stmt_end;
	sqlite3_stmt*  stmt_save;
	sqlite3_stmt** stmt_load;

	// sqlite3 indices
	int idx_save_id;
	int idx_save_blob;
	int idx_load_id;
} osmdb_cache_t;

osmdb_cache_t* osmdb_cache_create(const char* fname,
                                  int64_t changeset,
                                  double latT, double lonL,
                                  double latB, double lonR);
osmdb_cache_t* osmdb_cache_import(const char* fname, int nth);
void           osmdb_cache_delete(osmdb_cache_t** _self);
int            osmdb_cache_save(osmdb_cache_t* self,
                                int zoom, int x, int y,
                                int size, const void* data);
int            osmdb_cache_load(osmdb_cache_t* self,
                                int tid,
                                int zoom, int x, int y,
                                void* priv,
                                osmdb_cacheLoaded_fn loaded_fn);
int64_t        osmdb_cache_changeset(osmdb_cache_t* self);
void           osmdb_cache_bounds(osmdb_cache_t* self,
                                  double* _latT,
                                  double* _lonL,
                                  double* _latB,
                                  double* _lonR);

#endif

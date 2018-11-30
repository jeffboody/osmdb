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

#ifndef osmdb_chunk_H
#define osmdb_chunk_H

#include "../a3d/a3d_hashmap.h"

#define OSMDB_CHUNK_COUNT 10000

typedef struct
{
	const char* base;
	double      idu;
	int         type;
	int         size;
	int         dirty;
	int         locked;

	// map from idl to node/way/relation/ref
	a3d_hashmap_t* hash;
} osmdb_chunk_t;

osmdb_chunk_t* osmdb_chunk_new(const char* base,
                               double idu, int type,
                               int import, int* dsize);
int            osmdb_chunk_delete(osmdb_chunk_t** _self,
                                  int* dsize);
void           osmdb_chunk_lock(osmdb_chunk_t* self);
void           osmdb_chunk_unlock(osmdb_chunk_t* self);
int            osmdb_chunk_locked(osmdb_chunk_t* self);
const void*    osmdb_chunk_find(osmdb_chunk_t* self,
                                double idl);
int            osmdb_chunk_add(osmdb_chunk_t* self,
                               const void* data,
                               double idl, int dsize);
int            osmdb_chunk_flush(osmdb_chunk_t* self);
void           osmdb_chunk_fname(const char* base,
                                 int type, double idu,
                                 char* fname);
void           osmdb_chunk_path(const char* base,
                                int type, char* path);

#endif

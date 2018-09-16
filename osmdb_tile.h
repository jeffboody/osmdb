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

#ifndef osmdb_tile_H
#define osmdb_tile_H

#include "a3d/a3d_hashmap.h"

typedef struct
{
	const char* base;
	int zoom;
	int x;
	int y;
	int size;
	int dirty;
	int locked;

	// map from id to const ONE
	a3d_hashmap_t* hash_nodes;
	a3d_hashmap_t* hash_ways;
	a3d_hashmap_t* hash_relations;
} osmdb_tile_t;

osmdb_tile_t* osmdb_tile_new(int zoom, int x, int y,
                             const char* base,
                             int import, int* dsize);
int           osmdb_tile_delete(osmdb_tile_t** _self,
                                int* dsize);
void          osmdb_tile_lock(osmdb_tile_t* self);
void          osmdb_tile_unlock(osmdb_tile_t* self);
int           osmdb_tile_locked(osmdb_tile_t* self);
int           osmdb_tile_find(osmdb_tile_t* self,
                              int type, double id);
int           osmdb_tile_add(osmdb_tile_t* self,
                             int type, double id);
void          osmdb_tile_fname(const char* base,
                               int zoom, int x, int y,
                               char* fname);

#endif

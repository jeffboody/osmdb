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

#ifndef osm_entry_H
#define osm_entry_H

#include <stdint.h>

#include "libcc/cc_map.h"
#include "osmdb_type.h"

#define OSMDB_ENTRY_SIZE 100

typedef struct osmdb_entry_s
{
	// state
	int     refcount;
	int     dirty;
	int     type;
	int64_t major_id;

	// packed data
	size_t max_size;
	size_t size;
	void*  data;

	// handles
	cc_map_t* map;
} osmdb_entry_t;

osmdb_entry_t* osmdb_entry_new(int type,
                               int64_t major_id);
void           osmdb_entry_delete(osmdb_entry_t** _self);
int            osmdb_entry_get(osmdb_entry_t* self,
                               int64_t minor_id,
                               osmdb_handle_t** _hnd);
void           osmdb_entry_put(osmdb_entry_t* self,
                               osmdb_handle_t** _hnd);
int            osmdb_entry_add(osmdb_entry_t* self,
                               int loaded,
                               size_t size,
                               const void* data);

#endif

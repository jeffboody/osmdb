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

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "osmdb_entry.h"

/***********************************************************
* private                                                  *
***********************************************************/

static void
osmdb_entry_unmap(osmdb_entry_t* self)
{
	ASSERT(self);

	if(self->map)
	{
		cc_mapIter_t* miter;
		miter = cc_map_head(self->map);
		while(miter)
		{
			osmdb_handle_t* hnd;
			hnd = (osmdb_handle_t*)
			      cc_map_remove(self->map, &miter);
			FREE(hnd);
		}

		cc_map_delete(&self->map);
	}
}

static int
osmdb_entry_map(osmdb_entry_t* self, size_t offset)
{
	ASSERT(self);

	// check if map is needed
	if(self->map && (offset == 0))
	{
		return 1;
	}
	else if(self->map == NULL)
	{
		self->map = cc_map_new();
		if(self->map == NULL)
		{
			return 0;
		}

		offset = 0;
	}

	// add handles to map
	int64_t         minor_id = 0;
	size_t          bsize    = 0;
	osmdb_handle_t* hnd      = NULL;
	while(offset < self->size)
	{
		// tiles only contain a single mapping at 0
		if((offset > 0) &&
		   (self->type < OSMDB_TYPE_TILEREF_COUNT))
		{
			break;
		}

		hnd = CALLOC(1, sizeof(osmdb_handle_t));
		if(hnd == NULL)
		{
			LOGE("CALLOC failed");
			return 0;
		}
		hnd->entry = self;

		if(self->type == OSMDB_TYPE_NODECOORD)
		{
			hnd->node_coord = (osmdb_nodeCoord_t*)
			                   (self->data + offset);
			bsize    = osmdb_nodeCoord_sizeof(hnd->node_coord);
			minor_id = hnd->node_coord->nid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_NODEINFO)
		{
			hnd->node_info = (osmdb_nodeInfo_t*)
			                 (self->data + offset);
			bsize    = osmdb_nodeInfo_sizeof(hnd->node_info);
			minor_id = hnd->node_info->nid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_WAYINFO)
		{
			hnd->way_info = (osmdb_wayInfo_t*)
			                 (self->data + offset);
			bsize    = osmdb_wayInfo_sizeof(hnd->way_info);
			minor_id = hnd->way_info->wid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_WAYRANGE)
		{
			hnd->way_range = (osmdb_wayRange_t*)
			                  (self->data + offset);
			bsize    = osmdb_wayRange_sizeof(hnd->way_range);
			minor_id = hnd->way_range->wid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_WAYNDS)
		{
			hnd->way_nds = (osmdb_wayNds_t*)
			                (self->data + offset);
			bsize    = osmdb_wayNds_sizeof(hnd->way_nds);
			minor_id = hnd->way_nds->wid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_RELINFO)
		{
			hnd->rel_info = (osmdb_relInfo_t*)
			                 (self->data + offset);
			bsize    = osmdb_relInfo_sizeof(hnd->rel_info);
			minor_id = hnd->rel_info->rid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_RELMEMBERS)
		{
			hnd->rel_members = (osmdb_relMembers_t*)
			                    (self->data + offset);
			bsize    = osmdb_relMembers_sizeof(hnd->rel_members);
			minor_id = hnd->rel_members->rid%OSMDB_ENTRY_SIZE;
		}
		else if(self->type == OSMDB_TYPE_RELRANGE)
		{
			hnd->rel_range = (osmdb_relRange_t*)
			                  (self->data + offset);
			bsize    = osmdb_relRange_sizeof(hnd->rel_range);
			minor_id = hnd->rel_range->rid%OSMDB_ENTRY_SIZE;
		}
		else if((self->type < OSMDB_TYPE_TILEREF_COUNT) &&
		        (offset == 0))
		{
			hnd->tile_refs = (osmdb_tileRefs_t*)
			                  (self->data + offset);
			bsize    = osmdb_tileRefs_sizeof(hnd->tile_refs);
			minor_id = 0;
		}
		else
		{
			LOGE("invalid type=%i, major_id=%" PRId64 ", offset=%" PRId64,
			     self->type, self->major_id, (int64_t) offset);
			goto fail_type;
		}

		if(cc_map_addp(self->map, (const void*) hnd,
		               sizeof(int64_t), &minor_id) == NULL)
		{
			LOGE("invalid type=%i, major_id=%" PRId64 ", minor_id=%" PRId64,
			     self->type, self->major_id, minor_id);
			goto fail_map;
		}

		offset += bsize;
	}

	// success
	return 1;

	// failure
	fail_map:
	fail_type:
		FREE(hnd);
	return 0;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_entry_t*
osmdb_entry_new(int type, int64_t major_id)
{
	osmdb_entry_t* self;
	self = (osmdb_entry_t*)
	       CALLOC(1, sizeof(osmdb_entry_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->type     = type;
	self->major_id = major_id;

	// map allocated on demand

	return self;
}

void osmdb_entry_delete(osmdb_entry_t** _self)
{
	ASSERT(_self);

	osmdb_entry_t* self = *_self;
	if(self)
	{
		if(self->refcount)
		{
			LOGE("invalid refcount=%i", self->refcount);
		}

		osmdb_entry_unmap(self);
		FREE(self->data);
		FREE(self);
		*_self = NULL;
	}
}

int
osmdb_entry_get(osmdb_entry_t* self, int64_t minor_id,
                osmdb_handle_t** _hnd)
{
	ASSERT(self);
	ASSERT(_hnd);

	if(osmdb_entry_map(self, 0) == 0)
	{
		return 0;
	}

	// note that it is not an error to return a NULL hnd
	cc_mapIter_t* miter;
	miter = cc_map_findp(self->map, sizeof(int64_t),
	                     &minor_id);
	if(miter)
	{
		*_hnd = (osmdb_handle_t*) cc_map_val(miter);
		++self->refcount;
	}
	else
	{
		*_hnd = NULL;
	}

	return 1;
}

void osmdb_entry_put(osmdb_entry_t* self,
                     osmdb_handle_t** _hnd)
{
	ASSERT(self);

	osmdb_handle_t* hnd = *_hnd;
	if(hnd)
	{
		--self->refcount;
		*_hnd = NULL;
	}
}

int osmdb_entry_add(osmdb_entry_t* self, int loaded,
                    size_t size, const void* data)
{
	ASSERT(self);
	ASSERT(data);

	// resize data buffer
	size_t offset = self->size;
	size_t size2  = self->size + size;
	if(self->max_size >= size2)
	{
		self->size = size2;
	}
	else
	{
		// unmap because REALLOC will change pointer addresses
		if(self->refcount)
		{
			LOGE("invalid refcount=%i", self->refcount);
			return 0;
		}
		osmdb_entry_unmap(self);

		// compute the new size
		size_t max_size2 = self->max_size;
		if(max_size2 == 0)
		{
			max_size2 = 32;
		}

		while(max_size2 < size2)
		{
			max_size2 *= 2;
		}

		void* data2 = REALLOC(self->data, max_size2);
		if(data2 == NULL)
		{
			LOGE("REALLOC failed");
			return 0;
		}

		self->max_size = max_size2;
		self->size     = size2;
		self->data     = data2;
	}

	// copy data
	memcpy(self->data + offset, data, size);

	// check if data must be saved
	if(loaded == 0)
	{
		self->dirty = 1;
	}

	// check if data must be mapped
	if(self->map)
	{
		if(osmdb_entry_map(self, offset) == 0)
		{
			return 0;
		}
	}

	return 1;
}

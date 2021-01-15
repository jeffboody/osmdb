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
		cc_mapIter_t  miterator;
		cc_mapIter_t* miter;
		miter = cc_map_head(self->map, &miterator);
		while(miter)
		{
			osmdb_blob_t* blob;
			blob = (osmdb_blob_t*)
			       cc_map_remove(self->map, &miter);
			FREE(blob);
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

	// add blobs to map
	int64_t       minor_id = 0;
	size_t        bsize    = 0;
	osmdb_blob_t* blob     = NULL;
	while(offset < self->size)
	{
		// tiles only contain a single mapping at 0
		if((offset > 0) &&
		   (self->type < OSMDB_BLOB_TYPE_TILE_COUNT))
		{
			break;
		}

		blob = CALLOC(1, sizeof(osmdb_blob_t));
		if(blob == NULL)
		{
			LOGE("CALLOC failed");
			return 0;
		}
		blob->priv = (void*) self;

		if(self->type == OSMDB_BLOB_TYPE_NODE_COORD)
		{
			blob->node_coord = (osmdb_blobNodeCoord_t*)
			                   (self->data + offset);
			bsize    = osmdb_blobNodeCoord_sizeof(blob->node_coord);
			minor_id = blob->node_coord->nid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_NODE_INFO)
		{
			blob->node_info = (osmdb_blobNodeInfo_t*)
			                  (self->data + offset);
			bsize    = osmdb_blobNodeInfo_sizeof(blob->node_info);
			minor_id = blob->node_info->nid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_WAY_INFO)
		{
			blob->way_info = (osmdb_blobWayInfo_t*)
			                 (self->data + offset);
			bsize    = osmdb_blobWayInfo_sizeof(blob->way_info);
			minor_id = blob->way_info->wid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_WAY_RANGE)
		{
			blob->way_range = (osmdb_blobWayRange_t*)
			                  (self->data + offset);
			bsize    = osmdb_blobWayRange_sizeof(blob->way_range);
			minor_id = blob->way_range->wid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_WAY_NDS)
		{
			blob->way_nds = (osmdb_blobWayNds_t*)
			                (self->data + offset);
			bsize    = osmdb_blobWayNds_sizeof(blob->way_nds);
			minor_id = blob->way_nds->wid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_REL_INFO)
		{
			blob->rel_info = (osmdb_blobRelInfo_t*)
			                 (self->data + offset);
			bsize    = osmdb_blobRelInfo_sizeof(blob->rel_info);
			minor_id = blob->rel_info->rid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_REL_MEMBERS)
		{
			blob->rel_members = (osmdb_blobRelMembers_t*)
			                    (self->data + offset);
			bsize    = osmdb_blobRelMembers_sizeof(blob->rel_members);
			minor_id = blob->rel_members->rid%OSMDB_BLOB_SIZE;
		}
		else if(self->type == OSMDB_BLOB_TYPE_REL_RANGE)
		{
			blob->rel_range = (osmdb_blobRelRange_t*)
			                  (self->data + offset);
			bsize    = osmdb_blobRelRange_sizeof(blob->rel_range);
			minor_id = blob->rel_range->rid%OSMDB_BLOB_SIZE;
		}
		else if((self->type < OSMDB_BLOB_TYPE_TILE_COUNT) &&
		        (offset == 0))
		{
			blob->tile = (osmdb_blobTile_t*)
			             (self->data + offset);
			bsize    = osmdb_blobTile_sizeof(blob->tile);
			minor_id = 0;
		}
		else
		{
			LOGE("invalid type=%i, major_id=%" PRId64 ", offset=%" PRId64,
			     self->type, self->major_id, (int64_t) offset);
			goto fail_type;
		}

		if(cc_map_addf(self->map, (const void*) blob,
		               "%" PRId64, minor_id) == 0)
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
		FREE(blob);
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
                osmdb_blob_t** _blob)
{
	ASSERT(self);
	ASSERT(_blob);

	cc_mapIter_t miterator;

	if(osmdb_entry_map(self, 0) == 0)
	{
		return 0;
	}

	// note that it is not an error to return a NULL blob
	*_blob = (osmdb_blob_t*)
	         cc_map_findf(self->map, &miterator,
	                      "%" PRId64, minor_id);
	if(*_blob)
	{
		++self->refcount;
	}

	return 1;
}

void osmdb_entry_put(osmdb_entry_t* self,
                     osmdb_blob_t** _blob)
{
	ASSERT(self);

	osmdb_blob_t* blob = *_blob;
	if(blob)
	{
		--self->refcount;
		*_blob = NULL;
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

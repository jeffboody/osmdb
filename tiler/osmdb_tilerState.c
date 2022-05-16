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

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/math/cc_vec3f.h"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_unit.h"
#include "terrain/terrain_util.h"
#include "osmdb_waySegment.h"
#include "osmdb_tilerState.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_tilerState_t* osmdb_tilerState_new(void)
{
	osmdb_tilerState_t* self;
	self = (osmdb_tilerState_t*)
	       CALLOC(1, sizeof(osmdb_tilerState_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->os = osmdb_ostream_new();
	if(self->os == NULL)
	{
		goto fail_os;
	}

	self->map_export = cc_map_new();
	if(self->map_export == NULL)
	{
		goto fail_map_export;
	}

	self->map_segs = cc_map_new();
	if(self->map_segs == NULL)
	{
		goto fail_map_segs;
	}

	self->mm_nds_join = cc_multimap_new(NULL);
	if(self->mm_nds_join == NULL)
	{
		goto fail_mm_nds_join;
	}

	// success
	return self;

	// failure
	fail_mm_nds_join:
		cc_map_delete(&self->map_segs);
	fail_map_segs:
		cc_map_delete(&self->map_export);
	fail_map_export:
		osmdb_ostream_delete(&self->os);
	fail_os:
		FREE(self);
	return NULL;
}

void osmdb_tilerState_delete(osmdb_tilerState_t** _self)
{
	ASSERT(_self);

	osmdb_tilerState_t* self = *_self;
	if(self)
	{
		// tiler must reset the tilerState when making
		// tiles to ensure that these objects are empty
		cc_multimap_delete(&self->mm_nds_join);
		cc_map_delete(&self->map_segs);
		cc_map_delete(&self->map_export);
		osmdb_ostream_delete(&self->os);
		FREE(self);
	}
}

int osmdb_tilerState_init(osmdb_tilerState_t* self,
                          int zoom, int x, int y)
{
	ASSERT(self);

	self->zoom = zoom;
	self->x    = x;
	self->y    = y;

	terrain_bounds(x, y, zoom, &self->latT, &self->lonL,
	               &self->latB, &self->lonR);

	// compute x,y for tile
	cc_vec3f_t pa;
	cc_vec3f_t pb;
	float onemi = cc_mi2m(5280.0f);
	terrain_geo2xyz(self->latT, self->lonL, onemi,
	                &pa.x, &pa.y, &pa.z);
	terrain_geo2xyz(self->latB, self->lonR, onemi,
	                &pb.x, &pb.y, &pb.z);

	// compute min_dist and scale min_dist since
	// each tile serves multiple zoom levels
	float s = 8.0f;
	if(zoom == 15)
	{
		// 2:16, 4:17, 8:18, 16:19, 32:20
		s *= 1.0f/32.0f;
	}
	else
	{
		s *= 1.0f/2.0f;
	}

	float pix = sqrtf(2*256.0f*256.0f);
	self->min_dist = s*cc_vec3f_distance(&pb, &pa)/pix;

	return 1;
}

void osmdb_tilerState_reset(osmdb_tilerState_t* self,
                            osmdb_index_t* index,
                            int discard_export)
{
	ASSERT(self);
	ASSERT(index);

	// map_export is a mapping from nid/wid to ONE
	// so we can simply discard the map references
	if(discard_export)
	{
		cc_map_discard(self->map_export);
	}

	// delete way segments
	cc_mapIter_t* miter;
	miter = cc_map_head(self->map_segs);
	while(miter)
	{
		osmdb_waySegment_t* seg;
		seg = (osmdb_waySegment_t*)
		      cc_map_remove(self->map_segs, &miter);
		osmdb_waySegment_delete(index, &seg);
	}

	// delete refs
	cc_multimapIter_t  mmiterator;
	cc_multimapIter_t* mmiter;
	mmiter = cc_multimap_head(self->mm_nds_join, &mmiterator);
	while(mmiter)
	{
		int64_t* ref;
		ref = (int64_t*)
		      cc_multimap_remove(self->mm_nds_join, &mmiter);
		FREE(ref);
	}
}

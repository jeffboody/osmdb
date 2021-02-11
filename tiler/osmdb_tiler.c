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
#include "osmdb_tiler.h"
#include "osmdb_waySegment.h"

const int OSMDB_ONE = 1;

#define OSMDB_QUADRANT_NONE   0
#define OSMDB_QUADRANT_TOP    1
#define OSMDB_QUADRANT_LEFT   2
#define OSMDB_QUADRANT_BOTTOM 3
#define OSMDB_QUADRANT_RIGHT  4

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_tiler_gatherNode(osmdb_tiler_t* self,
                       int tid,
                       int64_t nid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	// check if node is already included by a relation
	cc_mapIter_t miterator;
	if(cc_map_findf(state->map_export, &miterator,
	                "n%" PRId64, nid))
	{
		return 1;
	}

	// handles may not exist due to osmosis
	osmdb_handle_t* hni;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_NODEINFO,
	                   nid, &hni) == 0)
	{
		return 0;
	}
	else if(hni == NULL)
	{
		return 1;
	}

	osmdb_handle_t* hnc;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_NODECOORD,
	                   nid, &hnc) == 0)
	{
		goto fail_coord;
	}
	else if(hnc == NULL)
	{
		osmdb_index_put(self->index, &hni);
		return 1;
	}

	if(osmdb_ostream_addNode(state->os,
	                         hni->node_info,
	                         hnc->node_coord) == 0)
	{
		goto fail_add;
	}

	osmdb_index_put(self->index, &hnc);
	osmdb_index_put(self->index, &hni);

	// success
	return 1;

	// failure
	fail_add:
		osmdb_index_put(self->index, &hnc);
	fail_coord:
		osmdb_index_put(self->index, &hni);
	return 0;
}

static int
osmdb_tiler_gatherNodes(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	int     type;
	int64_t id;
	if(state->zoom == 14)
	{
		type = OSMDB_TYPE_TILEREF_NODE14;
		id   = 16384*state->y + state->x; // 2^14
	}
	else if(state->zoom == 11)
	{
		type = OSMDB_TYPE_TILEREF_NODE11;
		id   = 2048*state->y + state->x; // 2^11
	}
	else
	{
		LOGE("invalid zoom=%i", state->zoom);
		return 0;
	}

	// handles may not exist due to osmosis
	osmdb_handle_t* htr;
	if(osmdb_index_get(self->index, tid,
	                   type, id, &htr) == 0)
	{
		return 0;
	}
	else if(htr == NULL)
	{
		return 1;
	}

	// gather nodes in tile
	int      i;
	int      ret   = 1;
	int      count = htr->tile_refs->count;
	int64_t* refs  = osmdb_tileRefs_refs(htr->tile_refs);
	for(i = 0; i < count; ++i)
	{
		if(osmdb_tiler_gatherNode(self, tid, refs[i]) == 0)
		{
			ret = 0;
			break;
		}
	}

	osmdb_index_put(self->index, &htr);

	return ret;
}

static int
osmdb_tiler_joinWay(osmdb_tiler_t* self, int tid,
                    osmdb_waySegment_t* a,
                    osmdb_waySegment_t* b,
                    int64_t ref1, int64_t* ref2)
{
	ASSERT(self);
	ASSERT(a);
	ASSERT(b);
	ASSERT(ref2);

	// don't join a way with itself
	if(a == b)
	{
		return 0;
	}

	// check if way is complete
	int64_t* refa1 = (int64_t*) cc_list_peekHead(a->list_nds);
	int64_t* refa2 = (int64_t*) cc_list_peekTail(a->list_nds);
	int64_t* refb1 = (int64_t*) cc_list_peekHead(b->list_nds);
	int64_t* refb2 = (int64_t*) cc_list_peekTail(b->list_nds);
	if((refa1 == NULL) || (refa2 == NULL) ||
	   (refb1 == NULL) || (refb2 == NULL))
	{
		return 0;
	}

	// only try to join ways with multiple nds
	if((cc_list_size(a->list_nds) < 2) ||
	   (cc_list_size(b->list_nds) < 2))
	{
		return 0;
	}

	// don't try to join loops
	if((*refa1 == *refa2) || (*refb1 == *refb2))
	{
		return 0;
	}

	// check if ref1 is included in both ways and that
	// they can be joined head to tail
	int append;
	int64_t* refp;
	int64_t* refn;
	cc_listIter_t* next;
	cc_listIter_t* prev;
	if((ref1 == *refa1) && (ref1 == *refb2))
	{
		append = 0;
		*ref2  = *refb1;

		prev = cc_list_next(cc_list_head(a->list_nds));
		next = cc_list_prev(cc_list_tail(b->list_nds));
		refp = (int64_t*) cc_list_peekIter(prev);
		refn = (int64_t*) cc_list_peekIter(next);
	}
	else if((ref1 == *refa2) && (ref1 == *refb1))
	{
		append = 1;
		*ref2  = *refb2;

		prev = cc_list_prev(cc_list_tail(a->list_nds));
		next = cc_list_next(cc_list_head(b->list_nds));
		refp = (int64_t*) cc_list_peekIter(prev);
		refn = (int64_t*) cc_list_peekIter(next);
	}
	else
	{
		return 0;
	}

	// identify the nodes to be joined
	osmdb_handle_t* hnc0 = NULL;
	osmdb_handle_t* hnc1 = NULL;
	osmdb_handle_t* hnc2 = NULL;
	if((osmdb_index_get(self->index, tid,
	                    OSMDB_TYPE_NODECOORD,
	                    *refp, &hnc0) == 0) ||
	   (osmdb_index_get(self->index, tid,
	                    OSMDB_TYPE_NODECOORD,
	                    ref1, &hnc1) == 0) ||
	   (osmdb_index_get(self->index, tid,
	                    OSMDB_TYPE_NODECOORD,
	                    *refn, &hnc2) == 0) ||
	   (hnc0 == NULL) || (hnc1 == NULL) ||
	   (hnc2 == NULL))
	{
		osmdb_index_put(self->index, &hnc0);
		osmdb_index_put(self->index, &hnc1);
		osmdb_index_put(self->index, &hnc2);
		return 0;
	}

	// check join angle to prevent joining ways
	// at a sharp angle since this causes weird
	// rendering artifacts
	cc_vec3f_t p0;
	cc_vec3f_t p1;
	cc_vec3f_t p2;
	cc_vec3f_t v01;
	cc_vec3f_t v12;
	osmdb_nodeCoord_t* nc0 = hnc0->node_coord;
	osmdb_nodeCoord_t* nc1 = hnc1->node_coord;
	osmdb_nodeCoord_t* nc2 = hnc2->node_coord;
	float onemi = cc_mi2m(5280.0f);
	terrain_geo2xyz(nc0->lat, nc0->lon, onemi,
	                &p0.x,  &p0.y, &p0.z);
	terrain_geo2xyz(nc1->lat, nc1->lon, onemi,
	                &p1.x,  &p1.y, &p1.z);
	terrain_geo2xyz(nc2->lat, nc2->lon, onemi,
	                &p2.x,  &p2.y, &p2.z);
	osmdb_index_put(self->index, &hnc0);
	osmdb_index_put(self->index, &hnc1);
	osmdb_index_put(self->index, &hnc2);
	cc_vec3f_subv_copy(&p1, &p0, &v01);
	cc_vec3f_subv_copy(&p2, &p1, &v12);
	cc_vec3f_normalize(&v01);
	cc_vec3f_normalize(&v12);
	float dot = cc_vec3f_dot(&v01, &v12);
	if(dot < cosf(cc_deg2rad(30.0f)))
	{
		return 0;
	}

	// check way attributes
	osmdb_wayInfo_t* ai = a->hwi->way_info;
	osmdb_wayInfo_t* bi = b->hwi->way_info;
	if((ai->class   != bi->class)  ||
	   (ai->layer   != bi->layer)  ||
	   (ai->flags   != bi->flags))
	{
		return 0;
	}

	// check name
	char* aname = osmdb_wayInfo_name(ai);
	char* bname = osmdb_wayInfo_name(bi);
	if(aname && bname)
	{
		if(strcmp(aname, bname) != 0)
		{
			return 0;
		}
	}
	else if(aname || bname)
	{
		return 0;
	}

	// join ways
	cc_listIter_t* iter;
	cc_listIter_t* temp;
	if(append)
	{
		// skip the first node
		iter = cc_list_head(b->list_nds);
		iter = cc_list_next(iter);
		while(iter)
		{
			temp = cc_list_next(iter);
			cc_list_swapn(b->list_nds, a->list_nds, iter, NULL);
			iter = temp;
		}
	}
	else
	{
		// skip the last node
		iter = cc_list_tail(b->list_nds);
		iter = cc_list_prev(iter);
		while(iter)
		{
			temp = cc_list_prev(iter);
			cc_list_swap(b->list_nds, a->list_nds, iter, NULL);
			iter = temp;
		}
	}

	// combine range
	if(b->way_range.latT > a->way_range.latT)
	{
		a->way_range.latT = b->way_range.latT;
	}
	if(b->way_range.lonL < a->way_range.lonL)
	{
		a->way_range.lonL = b->way_range.lonL;
	}
	if(b->way_range.latB < a->way_range.latB)
	{
		a->way_range.latB = b->way_range.latB;
	}
	if(b->way_range.lonR > a->way_range.lonR)
	{
		a->way_range.lonR = b->way_range.lonR;
	}

	return 1;
}

static int
osmdb_tiler_joinWays(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	osmdb_waySegment_t* seg1;
	osmdb_waySegment_t* seg2;
	cc_multimapIter_t   miterator1;
	cc_multimapIter_t*  miter1;
	cc_multimapIter_t   miterator2;
	cc_mapIter_t        hiter1;
	cc_mapIter_t        hiterator2;
	cc_mapIter_t*       hiter2;
	cc_listIter_t*      iter1;
	cc_listIter_t*      iter2;
	cc_list_t*          list1;
	cc_list_t*          list2;
	int64_t*            id1;
	int64_t*            id2;
	int64_t             ref1;
	int64_t             ref2;
	miter1 = cc_multimap_head(state->mm_nds_join,
	                          &miterator1);
	while(miter1)
	{
		ref1  = (int64_t)
		        strtoll(cc_multimap_key(miter1), NULL, 0);
		list1 = (cc_list_t*) cc_multimap_list(miter1);
		iter1 = cc_list_head(list1);
		while(iter1)
		{
			id1  = (int64_t*) cc_list_peekIter(iter1);
			if(*id1 == -1)
			{
				iter1 = cc_list_next(iter1);
				continue;
			}

			seg1 = (osmdb_waySegment_t*)
			       cc_map_findf(state->map_segs, &hiter1,
			                    "%" PRId64, *id1);
			if(seg1 == NULL)
			{
				iter1 = cc_list_next(iter1);
				continue;
			}

			iter2 = cc_list_next(iter1);
			while(iter2)
			{
				hiter2 = &hiterator2;
				id2 = (int64_t*) cc_list_peekIter(iter2);
				if(*id2 == -1)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				seg2 = (osmdb_waySegment_t*)
				       cc_map_findf(state->map_segs, hiter2,
				                    "%" PRId64, *id2);
				if(seg2 == NULL)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				if(osmdb_tiler_joinWay(self, tid, seg1, seg2,
				                       ref1, &ref2) == 0)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				// replace ref2->id2 with ref2->id1 in
				// mm_nds_join
				list2 = (cc_list_t*)
				        cc_multimap_findf(state->mm_nds_join,
				                          &miterator2,
				                          "%" PRId64, ref2);
				iter2 = cc_list_head(list2);
				while(iter2)
				{
					int64_t* idx = (int64_t*) cc_list_peekIter(iter2);
					if(*idx == *id2)
					{
						*idx = *id1;
						break;
					}

					iter2 = cc_list_next(iter2);
				}

				// remove segs from mm_nds_join
				*id1 = -1;
				*id2 = -1;

				// remove seg2 from map_segs
				cc_map_remove(state->map_segs, &hiter2);
				osmdb_waySegment_delete(self->index, &seg2);
				iter2 = NULL;
			}

			iter1 = cc_list_next(iter1);
		}

		miter1 = cc_multimap_nextList(miter1);
	}

	return 1;
}

static int
osmdb_tiler_sampleWay(osmdb_tiler_t* self, int tid,
                      int free_ref, cc_list_t* list_nds)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	int first = 1;

	cc_vec3f_t p0 = { .x=0.0f, .y=0.0f, .z=0.0f };

	cc_listIter_t* iter;
	iter = cc_list_head(list_nds);
	while(iter)
	{
		int64_t* _ref = (int64_t*) cc_list_peekIter(iter);

		// handles may not exist due to osmosis
		osmdb_handle_t* hnc = NULL;
		if(osmdb_index_get(self->index, tid,
		                   OSMDB_TYPE_NODECOORD,
		                   *_ref, &hnc) == 0)
		{
			return 0;
		}
		else if(hnc == NULL)
		{
			iter = cc_list_next(iter);
			continue;
		}

		// accept the last nd
		cc_listIter_t* next = cc_list_next(iter);
		if(next == NULL)
		{
			osmdb_index_put(self->index, &hnc);
			return 1;
		}

		// compute distance between points
		double     lat   = hnc->node_coord->lat;
		double     lon   = hnc->node_coord->lon;
		float      onemi = cc_mi2m(5280.0f);
		cc_vec3f_t p1;
		terrain_geo2xyz(lat, lon, onemi,
		                &p1.x, &p1.y, &p1.z);
		float dist = cc_vec3f_distance(&p1, &p0);

		// check if the nd should be kept or discarded
		if(first || (dist >= state->min_dist))
		{
			cc_vec3f_copy(&p1, &p0);
			iter = cc_list_next(iter);
		}
		else
		{
			cc_list_remove(list_nds, &iter);
			if(free_ref)
			{
				FREE(_ref);
			}
		}

		first = 0;
		osmdb_index_put(self->index, &hnc);
	}

	return 1;
}

static int
osmdb_tiler_sampleWays(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(state->map_segs, &miterator);
	while(miter)
	{
		osmdb_waySegment_t* seg;
		seg = (osmdb_waySegment_t*) cc_map_val(miter);
		if(osmdb_tiler_sampleWay(self, tid, 1,
		                         seg->list_nds) == 0)
		{
			return 0;
		}

		miter = cc_map_next(miter);
	}

	return 1;
}

static double osmdb_dot(double* a, double* b)
{
	ASSERT(a);
	ASSERT(b);

	return a[0]*b[0] + a[1]*b[1];
}

static int osmdb_quadrant(double* pc, double* tlc, double* trc)
{
	ASSERT(pc);
	ASSERT(tlc);
	ASSERT(trc);

	double tl = osmdb_dot(tlc, pc);
	double tr = osmdb_dot(trc, pc);

	if((tl > 0.0f) && (tr > 0.0f))
	{
		return OSMDB_QUADRANT_TOP;
	}
	else if((tl > 0.0f) && (tr <= 0.0f))
	{
		return OSMDB_QUADRANT_LEFT;
	}
	else if((tl <= 0.0f) && (tr <= 0.0f))
	{
		return OSMDB_QUADRANT_BOTTOM;
	}
	return OSMDB_QUADRANT_RIGHT;
}

static void osmdb_normalize(double* p)
{
	ASSERT(p);

	double mag = sqrt(p[0]*p[0] + p[1]*p[1]);
	p[0] = p[0]/mag;
	p[1] = p[1]/mag;
}

static int
osmdb_tiler_clipWay(osmdb_tiler_t* self, int tid,
                    osmdb_waySegment_t* seg,
                    double latT, double lonL,
                    double latB, double lonR)
{
	ASSERT(self);

	// don't clip short segs
	if(cc_list_size(seg->list_nds) <= 2)
	{
		return 1;
	}

	int64_t* first;
	int64_t* last;
	first = (int64_t*) cc_list_peekHead(seg->list_nds);
	last  = (int64_t*) cc_list_peekTail(seg->list_nds);

	// check if way forms a loop
	int loop  = 0;
	if(*first == *last)
	{
		loop = 1;
	}

	/*
	 * quadrant setup
	 * remove (B), (E), (F), (L)
	 * remove A as well if not loop
	 *  \                          /
	 *   \        (L)             /
	 *    \      M        K      /
	 *  A  +--------------------+
	 *     |TLC        J     TRC|
	 *     |     N              | I
	 *     |                    |
	 * (B) |                    |
	 *     |         *          |
	 *     |         CENTER     |
	 *     |                    | H
	 *     |                    |
	 *   C +--------------------+
	 *    /                G     \
	 *   /  D          (F)        \
	 *  /         (E)              \
	 */
	int q0 = OSMDB_QUADRANT_NONE;
	int q1 = OSMDB_QUADRANT_NONE;
	int q2 = OSMDB_QUADRANT_NONE;
	double dlat = (latT - latB)/2.0;
	double dlon = (lonR - lonL)/2.0;
	double center[2] =
	{
		lonL + dlon,
		latB + dlat
	};
	double tlc[2] =
	{
		(lonL - center[0])/dlon,
		(latT - center[1])/dlat
	};
	double trc[2] =
	{
		(lonR - center[0])/dlon,
		(latT - center[1])/dlat
	};
	osmdb_normalize(tlc);
	osmdb_normalize(trc);

	// clip way
	int64_t*        _ref;
	osmdb_handle_t* hnc = NULL;
	cc_listIter_t*  iter;
	cc_listIter_t*  prev = NULL;
	iter = cc_list_head(seg->list_nds);
	while(iter)
	{
		_ref = (int64_t*) cc_list_peekIter(iter);

		if(osmdb_index_get(self->index, tid,
		                   OSMDB_TYPE_NODECOORD,
		                   *_ref, &hnc) == 0)
		{
			return 0;
		}
		else if(hnc == NULL)
		{
			// ignore
			iter = cc_list_next(iter);
			continue;
		}

		// check if node is clipped
		if((hnc->node_coord->lat < latB) ||
		   (hnc->node_coord->lat > latT) ||
		   (hnc->node_coord->lon > lonR) ||
		   (hnc->node_coord->lon < lonL))
		{
			// proceed to clipping
		}
		else
		{
			// not clipped by tile
			q0   = OSMDB_QUADRANT_NONE;
			q1   = OSMDB_QUADRANT_NONE;
			prev = NULL;
			iter = cc_list_next(iter);
			osmdb_index_put(self->index, &hnc);
			continue;
		}

		// compute the quadrant
		double pc[2] =
		{
			(hnc->node_coord->lon - center[0])/dlon,
			(hnc->node_coord->lat - center[1])/dlat
		};
		osmdb_normalize(pc);
		q2 = osmdb_quadrant(pc, tlc, trc);

		// mark the first and last node
		int clip_last = 0;
		if(iter == cc_list_head(seg->list_nds))
		{
			if(loop)
			{
				q0 = OSMDB_QUADRANT_NONE;
				q1 = OSMDB_QUADRANT_NONE;
			}
			else
			{
				q0 = q2;
				q1 = q2;
			}
			prev = iter;
			iter = cc_list_next(iter);
			osmdb_index_put(self->index, &hnc);
			continue;
		}
		else if(iter == cc_list_tail(seg->list_nds))
		{
			if((loop == 0) && (q1 == q2))
			{
				clip_last = 1;
			}
			else
			{
				// don't clip the prev node when
				// keeping the last node
				prev = NULL;
			}
		}

		// clip prev node
		if(prev && (q0 == q2) && (q1 == q2))
		{
			_ref = (int64_t*) cc_list_remove(seg->list_nds, &prev);
			FREE(_ref);
		}

		// clip last node
		if(clip_last)
		{
			_ref = (int64_t*) cc_list_remove(seg->list_nds, &iter);
			FREE(_ref);
			osmdb_index_put(self->index, &hnc);
			return 1;
		}

		q0   = q1;
		q1   = q2;
		prev = iter;
		iter = cc_list_next(iter);
		osmdb_index_put(self->index, &hnc);
	}

	return 1;
}

static int
osmdb_tiler_clipWays(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	// elements are defined with zero width but in
	// practice are drawn with non-zero width
	// points/lines so an offset is needed to ensure they
	// are not clipped between neighboring tiles
	double dlat = (state->latT - state->latB)/16.0;
	double dlon = (state->lonR - state->lonL)/16.0;
	double latT = state->latT + dlat;
	double lonL = state->lonL - dlon;
	double latB = state->latB - dlat;
	double lonR = state->lonR + dlon;

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(state->map_segs, &miterator);
	while(miter)
	{
		osmdb_waySegment_t* seg;
		seg = (osmdb_waySegment_t*) cc_map_val(miter);

		if(osmdb_tiler_clipWay(self, tid, seg,
		                       latT, lonL, latB, lonR) == 0)
		{
			return 0;
		}

		miter = cc_map_next(miter);
	}

	return 1;
}

static int
osmdb_tiler_gatherWay(osmdb_tiler_t* self,
                      int tid, int64_t wid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	// check if way is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(state->map_export, &miterator,
	                "w%" PRId64, wid))
	{
		return 1;
	}

	// create segment
	// segment may not exist due to osmosis
	osmdb_waySegment_t* seg = NULL;
	if(osmdb_waySegment_new(self->index, tid, wid, &seg) == 0)
	{
		return 0;
	}
	else if(seg == NULL)
	{
		return 1;
	}

	if(cc_map_addf(state->map_segs, (const void*) seg,
	               "%" PRId64, wid) == 0)
	{
		osmdb_waySegment_delete(self->index, &seg);
		return 0;
	}

	// check if seg is complete
	int64_t* ref1;
	int64_t* ref2;
	ref1 = (int64_t*) cc_list_peekHead(seg->list_nds);
	ref2 = (int64_t*) cc_list_peekTail(seg->list_nds);
	if((ref1 == NULL) || (ref2 == NULL))
	{
		return 1;
	}

	// otherwise add join nds
	int64_t* id1_copy = (int64_t*)
	                    MALLOC(sizeof(int64_t));
	if(id1_copy == NULL)
	{
		return 0;
	}
	*id1_copy = wid;

	int64_t* id2_copy = (int64_t*)
	                    MALLOC(sizeof(int64_t));
	if(id2_copy == NULL)
	{
		FREE(id1_copy);
		return 0;
	}
	*id2_copy = wid;

	if(cc_multimap_addf(state->mm_nds_join,
	                    (const void*) id1_copy,
	                    "%" PRId64, *ref1) == 0)
	{
		FREE(id1_copy);
		FREE(id2_copy);
		return 0;
	}

	if(cc_multimap_addf(state->mm_nds_join,
	                    (const void*) id2_copy,
	                    "%" PRId64, *ref2) == 0)
	{
		FREE(id2_copy);
		return 0;
	}

	return 1;
}

static int
osmdb_tiler_exportWay(osmdb_tiler_t* self, int tid,
                      osmdb_waySegment_t* seg)
{
	ASSERT(self);
	ASSERT(seg);

	osmdb_tilerState_t* state = self->state[tid];

	if(osmdb_ostream_beginWay(state->os,
	                          seg->hwi->way_info,
	                          &seg->way_range, 0) == 0)
	{
		return 0;
	}

	osmdb_handle_t* hnc = NULL;
	cc_listIter_t*  iter;
	iter = cc_list_head(seg->list_nds);
	while(iter)
	{
		int64_t* ref = (int64_t*) cc_list_peekIter(iter);

		// handles may not exist due to osmosis
		if(osmdb_index_get(self->index, tid,
		                   OSMDB_TYPE_NODECOORD,
		                   *ref, &hnc) == 0)
		{
			return 0;
		}
		else if(hnc == NULL)
		{
			iter = cc_list_next(iter);
			continue;
		}

		if(osmdb_ostream_addWayCoord(state->os,
		                             hnc->node_coord) == 0)
		{
			goto fail_way_coord;
		}

		osmdb_index_put(self->index, &hnc);

		iter = cc_list_next(iter);
	}

	osmdb_ostream_endWay(state->os);

	// success
	return 1;

	// failure
	fail_way_coord:
		osmdb_index_put(self->index, &hnc);
	return 0;
}

static int
osmdb_tiler_exportWays(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	osmdb_waySegment_t* seg;
	cc_mapIter_t        miterator;
	cc_mapIter_t*       miter;
	miter = cc_map_head(state->map_segs, &miterator);
	while(miter)
	{
		seg = (osmdb_waySegment_t*) cc_map_val(miter);
		if(osmdb_tiler_exportWay(self, tid, seg) == 0)
		{
			return 0;
		}

		miter = cc_map_next(miter);
	}

	return 1;
}

static int
osmdb_tiler_gatherWays(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	int     type;
	int64_t id;
	if(state->zoom == 14)
	{
		type = OSMDB_TYPE_TILEREF_WAY14;
		id   = 16384*state->y + state->x; // 2^14
	}
	else if(state->zoom == 11)
	{
		type = OSMDB_TYPE_TILEREF_WAY11;
		id   = 2048*state->y + state->x; // 2^11
	}
	else
	{
		LOGE("invalid zoom=%i", state->zoom);
		return 0;
	}

	// handles may not exist due to osmosis
	osmdb_handle_t* htr;
	if(osmdb_index_get(self->index, tid,
	                   type, id, &htr) == 0)
	{
		return 0;
	}
	else if(htr == NULL)
	{
		return 1;
	}

	int      i;
	int      count = htr->tile_refs->count;
	int64_t* refs  = osmdb_tileRefs_refs(htr->tile_refs);
	for(i = 0; i < count; ++i)
	{
		if(osmdb_tiler_gatherWay(self, tid, refs[i]) == 0)
		{
			goto fail_gather_way;
		}
	}

	if(osmdb_tiler_joinWays(self, tid) == 0)
	{
		goto fail_join;
	}

	if(osmdb_tiler_sampleWays(self, tid) == 0)
	{
		goto fail_sample;
	}

	if(osmdb_tiler_clipWays(self, tid) == 0)
	{
		goto fail_clip;
	}

	if(osmdb_tiler_exportWays(self, tid) == 0)
	{
		goto fail_export;
	}

	osmdb_index_put(self->index, &htr);

	// success
	return 1;

	fail_export:
	fail_clip:
	fail_sample:
	fail_join:
	fail_gather_way:
		osmdb_index_put(self->index, &htr);
	return 0;
}

static int
osmdb_tiler_initListNds(osmdb_tiler_t* self, int tid,
                        osmdb_wayNds_t* way_nds)
{
	ASSERT(way_nds);

	osmdb_tilerState_t* state = self->state[tid];

	// reset state
	cc_list_discard(state->list_nds);

	// update state
	int i;
	int64_t* nds = osmdb_wayNds_nds(way_nds);
	for(i = 0; i < way_nds->count; ++i)
	{
		if(cc_list_append(state->list_nds, NULL,
		                  (const void*) &(nds[i])) == NULL)
		{
			goto fail_append;
		}
	}

	// success
	return 1;

	// failure
	fail_append:
		cc_list_discard(state->list_nds);
	return 0;
}

static int
osmdb_tiler_gatherWayNds(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	// gather nds
	cc_listIter_t* iter;
	iter = cc_list_head(state->list_nds);
	while(iter)
	{
		int64_t* _ref = (int64_t*) cc_list_peekIter(iter);

		// handles may not exist due to osmosis
		osmdb_handle_t* hnc;
		if(osmdb_index_get(self->index, tid,
		                   OSMDB_TYPE_NODECOORD,
		                   *_ref, &hnc) == 0)
		{
			return 0;
		}
		else if(hnc == NULL)
		{
			iter = cc_list_next(iter);
			continue;
		}

		if(osmdb_ostream_addWayCoord(state->os,
			                         hnc->node_coord) == 0)
		{
			osmdb_index_put(self->index, &hnc);
			return 0;
		}

		osmdb_index_put(self->index, &hnc);

		iter = cc_list_next(iter);
	}

	return 1;
}

static int
osmdb_tiler_gatherMemberWay(osmdb_tiler_t* self,
                            int tid, osmdb_relData_t* data,
                            int class, const char* name)
{
	// name may be NULL
	ASSERT(self);
	ASSERT(data);

	osmdb_tilerState_t* state = self->state[tid];

	// handles may not exist due to osmosis
	osmdb_handle_t* hwi;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_WAYINFO,
	                   data->wid, &hwi) == 0)
	{
		return 0;
	}
	else if(hwi == NULL)
	{
		return 1;
	}

	osmdb_handle_t* hwn;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_WAYNDS,
	                   data->wid, &hwn) == 0)
	{
		goto fail_nds;
	}
	else if(hwn == NULL)
	{
		osmdb_index_put(self->index, &hwi);
		return 1;
	}

	osmdb_handle_t* hwr;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_WAYRANGE,
	                   data->wid, &hwr) == 0)
	{
		goto fail_range;
	}
	else if(hwr == NULL)
	{
		osmdb_index_put(self->index, &hwn);
		osmdb_index_put(self->index, &hwi);
		return 1;
	}

	if(osmdb_tiler_initListNds(self, tid,
	                           hwn->way_nds) == 0)
	{
		goto fail_list_nds;
	}

	int flags = 0;
	if(data->inner)
	{
		flags = OSMDB_WAY_FLAG_INNER;
	}

	if(osmdb_ostream_beginWay(state->os, hwi->way_info,
	                          hwr->way_range, flags) == 0)
	{
		goto fail_begin;
	}

	if(osmdb_tiler_sampleWay(self, tid, 0,
	                         state->list_nds) == 0)
	{
		goto fail_sample;
	}

	if(osmdb_tiler_gatherWayNds(self, tid) == 0)
	{
		goto fail_gather;
	}

	osmdb_ostream_endWay(state->os);

	// mark way as found if class or name matches rel
	int         way_class = hwi->way_info->class;
	const char* way_name  = osmdb_wayInfo_name(hwi->way_info);
	if((class == way_class) ||
	   (name && way_name && (strcmp(name, way_name) == 0)))
	{
		if(cc_map_addf(state->map_export,
		               (const void*) &OSMDB_ONE,
		               "w%" PRId64, data->wid) == 0)
		{
			goto fail_mark;
		}
	}

	osmdb_index_put(self->index, &hwr);
	osmdb_index_put(self->index, &hwn);
	osmdb_index_put(self->index, &hwi);

	// success
	return 1;

	// failure
	fail_mark:
	fail_gather:
	fail_sample:
	fail_begin:
	fail_list_nds:
		osmdb_index_put(self->index, &hwr);
	fail_range:
		osmdb_index_put(self->index, &hwn);
	fail_nds:
		osmdb_index_put(self->index, &hwi);
	return 0;
}

static int
osmdb_tiler_gatherRel(osmdb_tiler_t* self,
                      int tid, int64_t rid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	// handles may not exist due to osmosis
	osmdb_handle_t* hri;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_RELINFO,
	                   rid, &hri) == 0)
	{
		return 0;
	}
	else if(hri == NULL)
	{
		return 1;
	}

	// members are optional
	osmdb_handle_t* hrm;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_RELMEMBERS,
	                   rid, &hrm) == 0)
	{
		goto fail_members;
	}

	osmdb_handle_t* hrr;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_RELRANGE,
	                   rid, &hrr) == 0)
	{
		goto fail_range;
	}
	else if(hrr == NULL)
	{
		osmdb_index_put(self->index, &hrm);
		osmdb_index_put(self->index, &hri);
		return 1;
	}

	// node_info is optional
	osmdb_handle_t* hni;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_NODEINFO,
	                   hri->rel_info->nid, &hni) == 0)
	{
		goto fail_node_info;
	}

	// node_coords is optional
	osmdb_handle_t* hnc;
	if(osmdb_index_get(self->index, tid,
	                   OSMDB_TYPE_NODECOORD,
	                   hri->rel_info->nid, &hnc) == 0)
	{
		goto fail_node_coord;
	}

	// get the rel/node name if it exists
	int         size_name = hri->rel_info->size_name;
	const char* name      = osmdb_relInfo_name(hri->rel_info);
	if((name == NULL) && hni && hni->node_info)
	{
		size_name = hni->node_info->size_name;
		name      = osmdb_nodeInfo_name(hni->node_info);
	}

	if(osmdb_ostream_beginRel(state->os,
	                          hri->rel_info,
	                          hrr->rel_range,
	                          size_name, name,
	                          hnc ? hnc->node_coord : NULL) == 0)
	{
		goto fail_begin_rel;
	}

	if(hrm)
	{
		int i;
		int count;
		osmdb_relData_t* data;
		data  = osmdb_relMembers_data(hrm->rel_members);
		count = hrm->rel_members->count;
		for(i = 0; i < count; ++i)
		{
			if(osmdb_tiler_gatherMemberWay(self, tid, &data[i],
			                               hri->rel_info->class,
			                               name) == 0)
			{
				goto fail_member;
			}
		}
	}

	osmdb_ostream_endRel(state->os);

	// mark node as found
	if(hni && hni->node_info)
	{
		if(cc_map_addf(state->map_export,
		               (const void*) &OSMDB_ONE, "n%" PRId64,
		               hni->node_info->nid) == 0)
		{
			goto fail_mark;
		}
	}

	osmdb_index_put(self->index, &hnc);
	osmdb_index_put(self->index, &hni);
	osmdb_index_put(self->index, &hrr);
	osmdb_index_put(self->index, &hrm);
	osmdb_index_put(self->index, &hri);

	// success
	return 1;

	// failure
	fail_mark:
	fail_member:
	fail_begin_rel:
		osmdb_index_put(self->index, &hnc);
	fail_node_coord:
		osmdb_index_put(self->index, &hni);
	fail_node_info:
		osmdb_index_put(self->index, &hrr);
	fail_range:
		osmdb_index_put(self->index, &hrm);
	fail_members:
		osmdb_index_put(self->index, &hri);
	return 0;
}

static int
osmdb_tiler_gatherRels(osmdb_tiler_t* self, int tid)
{
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	int     type;
	int64_t id;
	if(state->zoom == 14)
	{
		type = OSMDB_TYPE_TILEREF_REL14;
		id   = 16384*state->y + state->x; // 2^14
	}
	else if(state->zoom == 11)
	{
		type = OSMDB_TYPE_TILEREF_REL11;
		id   = 2048*state->y + state->x; // 2^11
	}
	else
	{
		LOGE("invalid zoom=%i", state->zoom);
		return 0;
	}

	// handles may not exist due to osmosis
	osmdb_handle_t* htr;
	if(osmdb_index_get(self->index, tid,
	                   type, id, &htr) == 0)
	{
		return 0;
	}
	else if(htr == NULL)
	{
		return 1;
	}

	// gather rels in tile
	int      i;
	int      ret   = 1;
	int      count = htr->tile_refs->count;
	int64_t* refs  = osmdb_tileRefs_refs(htr->tile_refs);
	for(i = 0; i < count; ++i)
	{
		if(osmdb_tiler_gatherRel(self, tid, refs[i]) == 0)
		{
			ret = 0;
			break;
		}
	}

	osmdb_index_put(self->index, &htr);

	return ret;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_tiler_t*
osmdb_tiler_new(osmdb_index_t* index, int nth)
{
	ASSERT(index);

	osmdb_tiler_t* self;
	self = (osmdb_tiler_t*)
	       CALLOC(1, sizeof(osmdb_tiler_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->index = index;
	self->nth   = nth;

	self->changeset = osmdb_index_changeset(index);
	if(self->changeset == 0)
	{
		goto fail_changeset;
	}

	self->state = (osmdb_tilerState_t**)
	              CALLOC(nth, sizeof(osmdb_tilerState_t*));
	if(self->state == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_state;
	}

	int i;
	for(i = 0; i < nth; ++i)
	{
		self->state[i] = osmdb_tilerState_new();
		if(self->state[i] == NULL)
		{
			goto fail_statei;
		}
	}

	// success
	return self;

	// failure
	fail_statei:
	{
		int j;
		for(j = 0; j < i; ++j)
		{
			osmdb_tilerState_delete(&self->state[j]);
		}
		FREE(self->state);
	}
	fail_state:
	fail_changeset:
		FREE(self);
	return NULL;
}

void osmdb_tiler_delete(osmdb_tiler_t** _self)
{
	ASSERT(_self);

	osmdb_tiler_t* self = *_self;
	if(self)
	{
		int i;
		for(i = 0; i < self->nth; ++i)
		{
			osmdb_tilerState_delete(&self->state[i]);
		}
		FREE(self->state);
		FREE(self);
		*_self = NULL;
	}
}

osmdb_tile_t*
osmdb_tiler_make(osmdb_tiler_t* self,
                 int tid, int zoom, int x, int y,
                 size_t* _size)
{
	// _size may be NULL
	ASSERT(self);

	osmdb_tilerState_t* state = self->state[tid];

	osmdb_index_lock(self->index);

	if(osmdb_tilerState_init(state, zoom, x, y) == 0)
	{
		goto fail_init;
	}

	if(osmdb_ostream_beginTile(state->os,
	                           zoom, x, y,
	                           self->changeset) == 0)
	{
		goto fail_begin;
	}

	if(osmdb_tiler_gatherRels(self, tid) == 0)
	{
		goto fail_gather_rels;
	}

	if(osmdb_tiler_gatherWays(self, tid) == 0)
	{
		goto fail_gather_ways;
	}

	if(osmdb_tiler_gatherNodes(self, tid) == 0)
	{
		goto fail_gather_nodes;
	}

	osmdb_tile_t* tile;
	tile = osmdb_ostream_endTile(state->os, _size);
	if(tile == NULL)
	{
		goto fail_end;
	}

	osmdb_tilerState_reset(state, self->index);
	osmdb_index_unlock(self->index);

	// success
	return tile;

	// failure
	fail_end:
	fail_gather_nodes:
	fail_gather_ways:
	fail_gather_rels:
		osmdb_tilerState_reset(state, self->index);
	fail_begin:
	fail_init:
		osmdb_index_unlock(self->index);
	return NULL;
}

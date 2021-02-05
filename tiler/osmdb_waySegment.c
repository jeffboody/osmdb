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
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "osmdb_waySegment.h"

/***********************************************************
* private                                                  *
***********************************************************/

int osmdb_waySegment_new(osmdb_index_t* index,
                         int tid, int64_t wid,
                         osmdb_waySegment_t** _seg)
{
	ASSERT(index);
	ASSERT(_seg);

	*_seg = NULL;

	osmdb_waySegment_t* seg;
	seg = (osmdb_waySegment_t*)
	      CALLOC(1, sizeof(osmdb_waySegment_t));
	if(seg == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	if(osmdb_index_get(index, tid,
	                   OSMDB_TYPE_WAYINFO,
	                   wid, &seg->hwi) == 0)
	{
		LOGE("invalid wid=%" PRId64, wid);
		goto fail_hwi;
	}
	else if(seg->hwi == NULL)
	{
		FREE(seg);
		return 1;
	}

	// copy range
	osmdb_handle_t* hwr = NULL;
	if(osmdb_index_get(index, tid,
	                   OSMDB_TYPE_WAYRANGE,
	                   wid, &hwr) == 0)
	{
		LOGE("invalid wid=%" PRId64, wid);
		goto fail_hwr;
	}
	else if(hwr == NULL)
	{
		osmdb_index_put(index, &seg->hwi);
		FREE(seg);
		return 1;
	}
	memcpy(&seg->way_range, hwr->way_range,
	       sizeof(osmdb_wayRange_t));

	// copy nds
	seg->list_nds = cc_list_new();
	if(seg->list_nds == NULL)
	{
		goto fail_list_nds;
	}

	osmdb_handle_t* hwn = NULL;
	if(osmdb_index_get(index, tid,
	                   OSMDB_TYPE_WAYNDS,
	                   wid, &hwn) == 0)
	{
		LOGE("invalid wid=%" PRId64, wid);
		goto fail_hwn;
	}
	else if(hwn == NULL)
	{
		cc_list_delete(&seg->list_nds);
		osmdb_index_put(index, &hwr);
		osmdb_index_put(index, &seg->hwi);
		FREE(seg);
		return 1;
	}

	int      i;
	int64_t* ref;
	osmdb_wayNds_t* way_nds = hwn->way_nds;
	int64_t* refs = osmdb_wayNds_nds(way_nds);
	for(i = 0; i < way_nds->count; ++i)
	{
		ref = (int64_t*) CALLOC(1, sizeof(int64_t));
		if(ref == NULL)
		{
			goto fail_ref;
		}
		*ref = refs[i];

		if(cc_list_append(seg->list_nds, NULL,
		                  (const void*) ref) == NULL)
		{
			goto fail_append;
		}
	}

	osmdb_index_put(index, &hwn);
	osmdb_index_put(index, &hwr);

	*_seg = seg;

	// success
	return 1;

	// failure
	fail_append:
		FREE(ref);
	fail_ref:
		osmdb_index_put(index, &hwn);
	fail_hwn:
	{
		cc_listIter_t* iter;
		iter = cc_list_head(seg->list_nds);
		while(iter)
		{
			ref = (int64_t*)
			      cc_list_remove(seg->list_nds, &iter);
			FREE(ref);
		}
		cc_list_delete(&seg->list_nds);
	}
	fail_list_nds:
		osmdb_index_put(index, &hwr);
	fail_hwr:
		osmdb_index_put(index, &seg->hwi);
	fail_hwi:
		FREE(seg);
	return 0;
}

void osmdb_waySegment_delete(osmdb_index_t* index,
                             osmdb_waySegment_t** _seg)
{
	ASSERT(index);
	ASSERT(_seg);

	osmdb_waySegment_t* seg = *_seg;
	if(seg)
	{
		cc_listIter_t* iter = cc_list_head(seg->list_nds);
		while(iter)
		{
			int64_t* ref;
			ref = (int64_t*) cc_list_remove(seg->list_nds, &iter);
			FREE(ref);
		}
		cc_list_delete(&seg->list_nds);

		osmdb_index_put(index, &seg->hwi);

		FREE(seg);
		*_seg = NULL;
	}
}

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/math/cc_vec3f.h"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_multimap.h"
#include "libcc/cc_timestamp.h"
#include "libcc/cc_unit.h"
#include "terrain/terrain_util.h"
#include "../osmdb_util.h"
#include "osmdb_entry.h"
#include "osmdb_index.h"

#define OSMDB_INDEX_CACHE_SIZE 4000000000

#define OSMDB_INDEX_BATCH_SIZE 10000

const char* OSMDB_INDEX_TBL[] =
{
	"tbl_nodeTile11",
	"tbl_nodeTile14",
	"tbl_wayTile11",
	"tbl_wayTile14",
	"tbl_relTile11",
	"tbl_relTile14",
	"tbl_nodeCoord",
	"tbl_nodeInfo",
	"tbl_wayInfo",
	"tbl_wayRange",
	"tbl_wayNds",
	"tbl_relInfo",
	"tbl_relMembers",
	"tbl_relRange",
	NULL
};

const int OSMDB_ONE = 1;

#define OSMDB_QUADRANT_NONE   0
#define OSMDB_QUADRANT_TOP    1
#define OSMDB_QUADRANT_LEFT   2
#define OSMDB_QUADRANT_BOTTOM 3
#define OSMDB_QUADRANT_RIGHT  4

/***********************************************************
* private - sqlite                                         *
***********************************************************/

static int
osmdb_index_createTables(osmdb_index_t* self)
{
	ASSERT(self);

	const char* sql_init[] =
	{
		"PRAGMA journal_mode = OFF;",
		"PRAGMA locking_mode = EXCLUSIVE;",
		"PRAGMA temp_store_directory = '.';",
		"CREATE TABLE tbl_attr"
		"("
		"	key TEXT UNIQUE,"
		"	val TEXT"
		");",
		NULL
	};

	int i = 0;
	while(sql_init[i])
	{
		if(sqlite3_exec(self->db, sql_init[i], NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec(%i): %s",
			     i, sqlite3_errmsg(self->db));
			return 0;
		}
		++i;
	}

	for(i = 0; i < OSMDB_TYPE_COUNT; ++i)
	{
		char sql_tbl[256];
		snprintf(sql_tbl, 256,
		         "CREATE TABLE %s"
		         "("
		         "	id   INTEGER PRIMARY KEY NOT NULL,"
		         "	blob BLOB"
		         ");", OSMDB_INDEX_TBL[i]);

		if(sqlite3_exec(self->db, sql_tbl, NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec(%i): %s",
			     i, sqlite3_errmsg(self->db));
			return 0;
		}
	}

	return 1;
}

static int osmdb_index_endTransaction(osmdb_index_t* self)
{
	ASSERT(self);

	int ret = 1;

	if((self->batch_size == 0) ||
	   (self->mode == OSMDB_INDEX_MODE_READONLY))
	{
		return 1;
	}

	sqlite3_stmt* stmt = self->stmt_end;
	if(sqlite3_step(stmt) == SQLITE_DONE)
	{
		self->batch_size = 0;
	}
	else
	{
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osmdb_index_beginTransaction(osmdb_index_t* self)
{
	ASSERT(self);

	int ret = 1;

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		return 1;
	}
	else if(self->batch_size >= OSMDB_INDEX_BATCH_SIZE)
	{
		if(osmdb_index_endTransaction(self) == 0)
		{
			return 0;
		}
	}
	else if(self->batch_size > 0)
	{
		++self->batch_size;
		return 1;
	}

	sqlite3_stmt* stmt = self->stmt_begin;
	if(sqlite3_step(stmt) == SQLITE_DONE)
	{
		++self->batch_size;
	}
	else
	{
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osmdb_index_load(osmdb_index_t* self, int tid,
                 osmdb_entry_t* entry)
{
	ASSERT(self);
	ASSERT(entry);

	int idx;
	int idx_id;
	sqlite3_stmt* stmt;

	idx    = OSMDB_TYPE_COUNT*tid + entry->type;
	stmt   = self->stmt_select[idx];
	idx_id = self->idx_select_id[idx];

	if(sqlite3_bind_int64(stmt, idx_id,
	                      entry->major_id) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int ret = 0;
	int step = sqlite3_step(stmt);
	if(step == SQLITE_ROW)
	{
		size_t      size;
		const void* data;
		size = (size_t) sqlite3_column_bytes(stmt, 0);
		data = sqlite3_column_blob(stmt, 0);
		if(data)
		{
			ret = osmdb_entry_add(entry, 1, size, data);
		}
		else
		{
			LOGE("data is NULL");
		}
	}
	else if(step == SQLITE_DONE)
	{
		ret = 1;
	}
	else
	{
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osmdb_index_save(osmdb_index_t* self,
                 osmdb_entry_t* entry)
{
	ASSERT(self);
	ASSERT(entry);

	int idx_id;
	int idx_blob;
	sqlite3_stmt* stmt;

	stmt     = self->stmt_insert[entry->type];
	idx_id   = self->idx_insert_id[entry->type];
	idx_blob = self->idx_insert_blob[entry->type];

	if((sqlite3_bind_int64(stmt, idx_id,
	                       entry->major_id) != SQLITE_OK) ||
	   (sqlite3_bind_blob(stmt, idx_blob,
	                      entry->data, (int) entry->size,
	                      SQLITE_TRANSIENT) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static void
osmdb_index_finalizeInsert(osmdb_index_t* self)
{
	ASSERT(self);

	int i;
	for(i = 0; i < OSMDB_TYPE_COUNT; ++i)
	{
		sqlite3_finalize(self->stmt_insert[i]);
		self->stmt_insert[i] = NULL;
	}
}

static void
osmdb_index_finalizeSelect(osmdb_index_t* self)
{
	ASSERT(self);

	if(self->stmt_select)
	{
		int count = self->nth*OSMDB_TYPE_COUNT;
		int i;
		for(i = 0; i < count; ++i)
		{
			sqlite3_finalize(self->stmt_select[i]);
			self->stmt_select[i] = NULL;
		}
		FREE(self->stmt_select);
		self->stmt_select = NULL;
	}

	FREE(self->idx_select_id);
	self->idx_select_id = NULL;
}

static int
osmdb_index_prepareInsert(osmdb_index_t* self)
{
	ASSERT(self);

	int i;
	for(i = 0; i < OSMDB_TYPE_COUNT; ++i)
	{
		char sql_insert[256];
		snprintf(sql_insert, 256,
		         "REPLACE INTO %s (id, blob)"
		         "	VALUES (@arg_id, @arg_blob);",
		         OSMDB_INDEX_TBL[i]);

		if(sqlite3_prepare_v2(self->db, sql_insert, -1,
		                      &self->stmt_insert[i],
		                      NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
			goto fail_prepare;
		}
		self->idx_insert_id[i]   = sqlite3_bind_parameter_index(self->stmt_insert[i],
		                                                        "@arg_id");
		self->idx_insert_blob[i] = sqlite3_bind_parameter_index(self->stmt_insert[i],
		                                                        "@arg_blob");
	}

	// success
	return 1;

	// finalize
	fail_prepare:
	{
		int j;
		for(j = 0; j < i; ++j)
		{
			sqlite3_finalize(self->stmt_insert[j]);
			self->stmt_insert[j] = NULL;
		}
	}
	return 0;
}

static int
osmdb_index_prepareSelect(osmdb_index_t* self)
{
	ASSERT(self);

	int count = self->nth*OSMDB_TYPE_COUNT;
	self->stmt_select = (sqlite3_stmt**)
	                    CALLOC(count, sizeof(sqlite3_stmt*));
	if(self->stmt_select == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	self->idx_select_id = (int*)
	                      CALLOC(count, sizeof(int));
	if(self->idx_select_id == NULL)
	{
		goto fail_idx_select_id;
	}

	int i;
	for(i = 0; i < count; ++i)
	{
		char sql_select[256];
		snprintf(sql_select, 256,
		         "SELECT blob FROM %s WHERE id=@arg_id;",
		         OSMDB_INDEX_TBL[i%OSMDB_TYPE_COUNT]);

		if(sqlite3_prepare_v2(self->db, sql_select, -1,
		                      &self->stmt_select[i],
		                      NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
			goto fail_prepare;
		}
		self->idx_select_id[i] = sqlite3_bind_parameter_index(self->stmt_select[i],
		                                                      "@arg_id");
	}

	// success
	return 1;

	// finalize
	fail_prepare:
	{
		int j;
		for(j = 0; j < i; ++j)
		{
			sqlite3_finalize(self->stmt_select[j]);
			self->stmt_select[j] = NULL;
		}
		FREE(self->idx_select_id);
	}
	fail_idx_select_id:
		FREE(self->stmt_select);
	return 0;
}

/***********************************************************
* private - locking                                        *
***********************************************************/

static void
osmdb_index_lock(osmdb_index_t* self)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		pthread_mutex_lock(&self->cache_mutex);
	}
}

static void
osmdb_index_unlock(osmdb_index_t* self, int signal)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		if(signal)
		{
			pthread_cond_broadcast(&self->cache_cond);
		}

		pthread_mutex_unlock(&self->cache_mutex);
	}
}

static void
osmdb_index_lockRead(osmdb_index_t* self)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		pthread_mutex_lock(&self->cache_mutex);

		// wait while the editor is needed
		while(self->cache_editor)
		{
			pthread_cond_wait(&self->cache_cond,
			                  &self->cache_mutex);
		}

		++self->cache_readers;

		pthread_mutex_unlock(&self->cache_mutex);
	}
}

static void
osmdb_index_lockReadUpdate(osmdb_index_t* self)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		// take exclusive lock
		pthread_mutex_lock(&self->cache_mutex);

		--self->cache_readers;
	}
}

static void
osmdb_index_lockLoad(osmdb_index_t* self,
                     int tid, int type, int id)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		pthread_mutex_lock(&self->cache_mutex);

		--self->cache_readers;
		if(self->cache_editor && (self->cache_readers == 0))
		{
			pthread_cond_broadcast(&self->cache_cond);
		}

		// wait while editor is needed or entry is loading
		int retry = 1;
		while(retry)
		{
			if(self->cache_editor)
			{
				pthread_cond_wait(&self->cache_cond,
				                  &self->cache_mutex);
				continue;
			}

			int i;
			retry = 0;
			for(i = 0; i < self->nth; ++i)
			{
				if((self->cache_loading[i].type == type) &&
				   (self->cache_loading[i].id   == id))
				{
					pthread_cond_wait(&self->cache_cond,
					                  &self->cache_mutex);
					retry = 1;
					break;
				}
			}
		}

		++self->cache_loaders;
		self->cache_loading[tid].type = type;
		self->cache_loading[tid].id   = id;

		pthread_mutex_unlock(&self->cache_mutex);
	}
}

static void
osmdb_index_lockLoadUpdate(osmdb_index_t* self, int tid)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		// take exclusive lock
		pthread_mutex_lock(&self->cache_mutex);

		--self->cache_loaders;
		self->cache_loading[tid].type = -1;
		self->cache_loading[tid].id   = -1;
	}
}

static void
osmdb_index_unlockLoadErr(osmdb_index_t* self, int tid)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		pthread_mutex_lock(&self->cache_mutex);

		--self->cache_loaders;
		self->cache_loading[tid].type = -1;
		self->cache_loading[tid].id   = -1;

		pthread_cond_broadcast(&self->cache_cond);
		pthread_mutex_unlock(&self->cache_mutex);
	}
}

static void
osmdb_index_lockEdit(osmdb_index_t* self, int tid)
{
	ASSERT(self);

	if(self->mode == OSMDB_INDEX_MODE_READONLY)
	{
		// take exclusive lock
		pthread_mutex_lock(&self->cache_mutex);

		// we do not need to signal if cache_loaders is zero
		// because this is the editor wait function
		--self->cache_loaders;

		// wait while users still accessing cache
		self->cache_editor = 1;
		while(self->cache_readers || self->cache_loaders)
		{
			pthread_cond_wait(&self->cache_cond,
			                  &self->cache_mutex);
		}

		self->cache_editor            = 0;
		self->cache_loading[tid].type = -1;
		self->cache_loading[tid].id   = -1;
	}
}

/***********************************************************
* private - cache                                          *
***********************************************************/

static int
osmdb_index_evict(osmdb_index_t* self,
                  osmdb_entry_t** _entry)
{
	ASSERT(self);
	ASSERT(_entry);

	if(osmdb_index_beginTransaction(self) == 0)
	{
		return 0;
	}

	int ret = 1;
	osmdb_entry_t* entry = *_entry;
	if(entry)
	{
		if(entry->dirty)
		{
			if(osmdb_index_save(self, entry) == 0)
			{
				ret = 0;
			}
		}

		osmdb_entry_delete(_entry);
	}

	return ret;
}

static int
osmdb_index_trim(osmdb_index_t* self)
{
	ASSERT(self);

	// note: synchronization is not required for trim since
	// it will only be called when locked or when in single
	// thread mode (e.g. CREATE or APPEND)

	int ret = 1;

	int first = 1;

	cc_listIter_t* iter = cc_list_head(self->cache_list);
	while(iter)
	{
		// check if cache is full
		// if we have exceeded the high water mark then
		// evict entries until we have reached the low water
		// mark to cause more entries to be batched in a
		// transaction
		size_t size = MEMSIZE();
		if(first)
		{
			if(size <= OSMDB_INDEX_CACHE_SIZE)
			{
				break;
			}

			first = 0;
		}
		if(size <= (size_t) (0.95f*OSMDB_INDEX_CACHE_SIZE))
		{
			break;
		}

		// skip entries that are in use
		osmdb_entry_t* entry;
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);
		if(entry->refcount)
		{
			iter = cc_list_next(iter);
			continue;
		}

		// remove the entry
		cc_mapIter_t  miterator;
		cc_mapIter_t* miter = &miterator;
		iter = (cc_listIter_t*)
		       cc_map_findf(self->cache_map, miter,
		                    "%i/%" PRId64,
		                    entry->type,
		                    entry->major_id);
		cc_map_remove(self->cache_map, &miter);
		cc_list_remove(self->cache_list, &iter);
		if(osmdb_index_evict(self, &entry) == 0)
		{
			ret = 0;
		}
	}

	if(osmdb_index_endTransaction(self) == 0)
	{
		ret = 0;
	}

	return ret;
}

/***********************************************************
* private - tile                                           *
***********************************************************/

typedef struct
{
	osmdb_handle_t* hnd_info;

	osmdb_wayRange_t way_range;

	cc_list_t* list_nds;
} osmdb_segment_t;

static int
osmdb_index_newSegment(osmdb_index_t* self,
                       int tid,
                       int64_t wid,
                       osmdb_segment_t** _seg)
{
	ASSERT(self);
	ASSERT(_seg);

	*_seg = NULL;

	osmdb_segment_t* seg;
	seg = (osmdb_segment_t*)
	      CALLOC(1, sizeof(osmdb_segment_t));
	if(seg == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	if((osmdb_index_get(self, tid, OSMDB_TYPE_WAYINFO,
	                    wid, &seg->hnd_info) == 0) ||
	   (seg->hnd_info == NULL))
	{
		LOGE("invalid wid=%" PRId64, wid);
		goto fail_hnd_info;
	}

	// copy range
	osmdb_handle_t* hnd_range = NULL;
	if((osmdb_index_get(self, tid, OSMDB_TYPE_WAYRANGE,
	                    wid, &hnd_range) == 0) ||
	    (hnd_range == NULL))
	{
		LOGE("invalid wid=%" PRId64, wid);
		goto fail_hnd_range;
	}
	memcpy(&seg->way_range, hnd_range->way_range,
	       sizeof(osmdb_wayRange_t));

	// copy nds
	seg->list_nds = cc_list_new();
	if(seg->list_nds == NULL)
	{
		goto fail_list_nds;
	}

	osmdb_handle_t* hnd_nds = NULL;
	if((osmdb_index_get(self, tid, OSMDB_TYPE_WAYNDS,
	                    wid, &hnd_nds) == 0) ||
	    (hnd_nds == NULL))
	{
		LOGE("invalid wid=%" PRId64, wid);
		goto fail_hnd_nds;
	}

	int      i;
	int64_t* ref;
	osmdb_wayNds_t* way_nds = hnd_nds->way_nds;
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

	// put temporary hnds
	osmdb_index_put(self, &hnd_nds);
	osmdb_index_put(self, &hnd_range);

	*_seg = seg;

	// success
	return 1;

	// failure
	fail_append:
		FREE(ref);
	fail_ref:
		osmdb_index_put(self, &hnd_nds);
	fail_hnd_nds:
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
		osmdb_index_put(self, &hnd_range);
	fail_hnd_range:
		osmdb_index_put(self, &seg->hnd_info);
	fail_hnd_info:
		FREE(seg);
	return 0;
}

static void
osmdb_index_deleteSegment(osmdb_index_t* self,
                          osmdb_segment_t** _seg)
{
	ASSERT(self);
	ASSERT(_seg);

	osmdb_segment_t* seg = *_seg;
	if(seg)
	{
		cc_listIter_t* iter = cc_list_head(seg->list_nds);
		while(iter)
		{
			int64_t* ref;
			ref = (int64_t*) cc_list_remove(seg->list_nds, &iter);
			FREE(ref);
		}

		osmdb_index_put(self, &seg->hnd_info);

		FREE(seg);
		*_seg = NULL;
	}
}

static int
osmdb_index_exportNode(osmdb_index_t* self,
                       osmdb_nodeInfo_t* node_info,
                       osmdb_nodeCoord_t* node_coord,
                       xml_ostream_t* os)
{
	// node_info may be NULL
	ASSERT(self);
	ASSERT(node_coord);
	ASSERT(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "node");
	ret &= xml_ostream_attrf(os, "id", "%" PRId64,
	                         node_coord->nid);
	ret &= xml_ostream_attrf(os, "lat", "%lf", node_coord->lat);
	ret &= xml_ostream_attrf(os, "lon", "%lf", node_coord->lon);
	if(node_info)
	{
		char* name = osmdb_nodeInfo_name(node_info);
		if(name)
		{
			ret &= xml_ostream_attr(os, "name", name);
		}
		if(node_info->ele)
		{
			ret &= xml_ostream_attrf(os, "ele", "%i", node_info->ele);
		}
		if(node_info->class)
		{
			ret &= xml_ostream_attr(os, "class",
			                        osmdb_classCodeToName(node_info->class));
		}
	}
	ret &= xml_ostream_end(os);

	return ret;
}

static int
osmdb_index_gatherNode(osmdb_index_t* self,
                       int tid,
                       int64_t nid,
                       cc_map_t* map_export,
                       xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	// check if node is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator, "n%" PRId64, nid))
	{
		return 1;
	}

	// info is optional
	osmdb_handle_t* bi;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_NODEINFO,
	                   nid, &bi) == 0)
	{
		return 0;
	}

	// coord is required
	osmdb_handle_t* bc;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_NODECOORD,
	                   nid, &bc) == 0)
	{
		goto fail_coord;
	}
	else if(bc == NULL)
	{
		return 1;
	}

	if(osmdb_index_exportNode(self,
	                          bi ? bi->node_info : NULL,
	                          bc->node_coord, os) == 0)
	{
		goto fail_export;
	}

	// mark the node as found
	if(cc_map_addf(map_export, (const void*) &OSMDB_ONE,
	               "n%" PRId64, nid) == 0)
	{
		goto fail_mark;
	}

	osmdb_index_put(self, &bc);
	osmdb_index_put(self, &bi);

	// success
	return 1;

	// failure
	fail_mark:
	fail_export:
		osmdb_index_put(self, &bc);
	fail_coord:
		osmdb_index_put(self, &bi);
	return 0;
}

static int
osmdb_index_sampleWay(osmdb_index_t* self,
                      int tid,
                      int zoom,
                      float min_dist,
                      int free_ref,
                      cc_list_t* list_nds)
{
	ASSERT(self);
	ASSERT(list_nds);

	osmdb_handle_t* hnd   = NULL;
	int             first = 1;
	cc_vec3f_t      p0    = { .x=0.0f, .y=0.0f, .z=0.0f };
	cc_listIter_t*  iter  = cc_list_head(list_nds);
	while(iter)
	{
		int64_t* ref = (int64_t*) cc_list_peekIter(iter);

		if(osmdb_index_get(self, tid,
		                   OSMDB_TYPE_NODECOORD,
		                   *ref, &hnd) == 0)
		{
			return 0;
		}
		else if(hnd == NULL)
		{
			iter = cc_list_next(iter);
			continue;
		}

		// accept the last nd
		cc_listIter_t* next = cc_list_next(iter);
		if(next == NULL)
		{
			osmdb_index_put(self, &hnd);
			return 1;
		}

		// compute distance between points
		double     lat   = hnd->node_coord->lat;
		double     lon   = hnd->node_coord->lon;
		float      onemi = cc_mi2m(5280.0f);
		cc_vec3f_t p1;
		terrain_geo2xyz(lat, lon, onemi,
		                &p1.x, &p1.y, &p1.z);
		float dist = cc_vec3f_distance(&p1, &p0);

		// check if the nd should be kept or discarded
		if(first || (dist >= min_dist))
		{
			cc_vec3f_copy(&p1, &p0);
			iter = cc_list_next(iter);
		}
		else
		{
			int64_t* ref;
			ref = (int64_t*) cc_list_remove(list_nds, &iter);
			if(free_ref)
			{
				FREE(ref);
			}
		}

		first = 0;
		osmdb_index_put(self, &hnd);
	}

	return 1;
}

static cc_list_t*
osmdb_index_ndsList(osmdb_wayNds_t* way_nds)
{
	ASSERT(way_nds);

	cc_list_t* list_nds = cc_list_new();
	if(list_nds == NULL)
	{
		return NULL;
	}

	int i;
	int64_t* nds = osmdb_wayNds_nds(way_nds);
	for(i = 0; i < way_nds->count; ++i)
	{
		if(cc_list_append(list_nds, NULL,
		                  (const void*) &(nds[i])) == NULL)
		{
			goto fail_append;
		}
	}

	// success
	return list_nds;

	// failure
	fail_append:
		cc_list_discard(list_nds);
		cc_list_delete(&list_nds);
	return NULL;
}

static int
osmdb_index_exportWay(osmdb_index_t* self,
                      osmdb_wayInfo_t* way_info,
                      osmdb_wayRange_t* way_range,
                      cc_list_t* list_nds,
                      xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(way_info);
	ASSERT(way_range);
	ASSERT(list_nds);
	ASSERT(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "way");
	ret &= xml_ostream_attrf(os, "id", "%" PRId64, way_info->wid);

	char* name = osmdb_wayInfo_name(way_info);
	if(name)
	{
		ret &= xml_ostream_attr(os, "name", name);
	}
	if(way_info->class)
	{
		ret &= xml_ostream_attr(os, "class",
		                        osmdb_classCodeToName(way_info->class));
	}
	if(way_info->layer)
	{
		ret &= xml_ostream_attrf(os, "layer", "%i",
		                         way_info->layer);
	}

	if(way_info->flags & OSMDB_WAYINFO_FLAG_FORWARD)
	{
		ret &= xml_ostream_attrf(os, "oneway", "1");
	}
	else if(way_info->flags & OSMDB_WAYINFO_FLAG_REVERSE)
	{
		ret &= xml_ostream_attrf(os, "oneway", "-1");
	}

	if(way_info->flags & OSMDB_WAYINFO_FLAG_BRIDGE)
	{
		ret &= xml_ostream_attrf(os, "bridge", "1");
	}
	if(way_info->flags & OSMDB_WAYINFO_FLAG_TUNNEL)
	{
		ret &= xml_ostream_attrf(os, "tunnel", "1");
	}
	if(way_info->flags & OSMDB_WAYINFO_FLAG_CUTTING)
	{
		ret &= xml_ostream_attrf(os, "cutting", "1");
	}
	if((way_range->latT == 0.0) && (way_range->lonL == 0.0) &&
	   (way_range->latB == 0.0) && (way_range->lonR == 0.0))
	{
		// skip range
	}
	else
	{
		ret &= xml_ostream_attrf(os, "latT", "%lf",
		                         way_range->latT);
		ret &= xml_ostream_attrf(os, "lonL", "%lf",
		                         way_range->lonL);
		ret &= xml_ostream_attrf(os, "latB", "%lf",
		                         way_range->latB);
		ret &= xml_ostream_attrf(os, "lonR", "%lf",
		                         way_range->lonR);
	}

	cc_listIter_t* iter = cc_list_head(list_nds);
	while(iter)
	{
		int64_t* ref = (int64_t*) cc_list_peekIter(iter);
		ret &= xml_ostream_begin(os, "nd");
		ret &= xml_ostream_attrf(os, "ref", "%" PRId64, *ref);
		ret &= xml_ostream_end(os);
		iter = cc_list_next(iter);
	}

	ret &= xml_ostream_end(os);

	return ret;
}

static int
osmdb_index_gatherMemberWay(osmdb_index_t* self,
                            int tid, int64_t wid,
                            int zoom, float min_dist,
                            cc_map_t* map_export,
                            xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	// check if way is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator,
	                "w%" PRId64, wid))
	{
		return 1;
	}

	// info may not exist due to osmosis
	osmdb_handle_t* bi;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_WAYINFO,
	                   wid, &bi) == 0)
	{
		return 0;
	}
	else if(bi == NULL)
	{
		return 1;
	}

	// nds are required
	osmdb_handle_t* bn;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_WAYNDS,
	                   wid, &bn) == 0)
	{
		goto fail_nds;
	}
	else if(bn == NULL)
	{
		LOGE("invalid nds");
		goto fail_nds;
	}

	// range is required
	osmdb_handle_t* br;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_WAYRANGE,
	                   wid, &br) == 0)
	{
		goto fail_range;
	}
	else if(br == NULL)
	{
		LOGE("invalid range");
		goto fail_range;
	}

	cc_list_t* list_nds;
	list_nds = osmdb_index_ndsList(bn->way_nds);
	if(list_nds == NULL)
	{
		goto fail_list_nds;
	}

	if(osmdb_index_sampleWay(self, tid, zoom, min_dist,
	                         0, list_nds) == 0)
	{
		goto fail_sample;
	}

	// gather nodes
	cc_listIter_t* iter;
	iter = cc_list_head(list_nds);
	while(iter)
	{
		int64_t* _ref = (int64_t*) cc_list_peekIter(iter);
		if(osmdb_index_gatherNode(self, tid, *_ref,
		                          map_export, os) == 0)
		{
			goto fail_nd;
		}

		iter = cc_list_next(iter);
	}

	if(osmdb_index_exportWay(self, bi->way_info,
	                         br->way_range,
	                         list_nds, os) == 0)
	{
		goto fail_export;
	}

	// mark the way as found
	if(cc_map_addf(map_export, (const void*) &OSMDB_ONE,
	               "w%" PRId64, wid) == 0)
	{
		goto fail_mark;
	}

	cc_list_discard(list_nds);
	cc_list_delete(&list_nds);
	osmdb_index_put(self, &br);
	osmdb_index_put(self, &bn);
	osmdb_index_put(self, &bi);

	// success
	return 1;

	// failure
	fail_mark:
	fail_export:
	fail_nd:
	fail_sample:
		cc_list_discard(list_nds);
		cc_list_delete(&list_nds);
	fail_list_nds:
		osmdb_index_put(self, &br);
	fail_range:
		osmdb_index_put(self, &bn);
	fail_nds:
		osmdb_index_put(self, &bi);
	return 0;
}

static int
osmdb_index_exportRel(osmdb_index_t* self,
                      osmdb_relInfo_t* rel_info,
                      osmdb_relMembers_t* rel_members,
                      osmdb_relRange_t* rel_range,
                      xml_ostream_t* os)
{
	// rel_members may be NULL
	ASSERT(self);
	ASSERT(rel_info);
	ASSERT(rel_range);
	ASSERT(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "relation");
	ret &= xml_ostream_attrf(os, "id", "%" PRId64, rel_info->rid);

	char* name = osmdb_relInfo_name(rel_info);
	if(name)
	{
		ret &= xml_ostream_attr(os, "name", name);
	}
	if(rel_info->class)
	{
		ret &= xml_ostream_attr(os, "class",
		                        osmdb_classCodeToName(rel_info->class));
	}
	if((rel_range->latT == 0.0) && (rel_range->lonL == 0.0) &&
	   (rel_range->latB == 0.0) && (rel_range->lonR == 0.0))
	{
		// skip range
	}
	else
	{
		ret &= xml_ostream_attrf(os, "latT", "%lf", rel_range->latT);
		ret &= xml_ostream_attrf(os, "lonL", "%lf", rel_range->lonL);
		ret &= xml_ostream_attrf(os, "latB", "%lf", rel_range->latB);
		ret &= xml_ostream_attrf(os, "lonR", "%lf", rel_range->lonR);
	}

	if(rel_members)
	{
		osmdb_relData_t* data;
		data = osmdb_relMembers_data(rel_members);

		int i;
		int count = rel_members->count;
		for(i = 0; i < count; ++i)
		{
			ret &= xml_ostream_begin(os, "member");
			ret &= xml_ostream_attr(os, "type",
			                        osmdb_relationMemberCodeToType(data[i].type));
			ret &= xml_ostream_attrf(os, "ref", "%0.0lf", data[i].ref);
			if(data[i].role)
			{
				ret &= xml_ostream_attr(os, "role",
				                        osmdb_relationMemberCodeToRole(data[i].role));
			}
			ret &= xml_ostream_end(os);
		}
	}

	ret &= xml_ostream_end(os);

	return ret;
}

static int
osmdb_index_gatherRel(osmdb_index_t* self,
                      int tid, int64_t rid,
                      int zoom, float min_dist,
                      cc_map_t* map_export,
                      xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	// check if relation is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator, "r%" PRId64, rid))
	{
		return 1;
	}

	// info may not exist due to osmosis
	osmdb_handle_t* bi;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_RELINFO,
	                   rid, &bi) == 0)
	{
		return 0;
	}
	else if(bi == NULL)
	{
		return 1;
	}

	// members are optional
	osmdb_handle_t* bm;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_RELMEMBERS,
	                   rid, &bm) == 0)
	{
		goto fail_members;
	}

	// range is required
	osmdb_handle_t* br;
	if(osmdb_index_get(self, tid, OSMDB_TYPE_RELRANGE,
	                   rid, &br) == 0)
	{
		goto fail_range;
	}
	else if(br == NULL)
	{
		LOGE("invalid range");
		goto fail_range;
	}

	if(bm)
	{
		int i;
		int count;
		osmdb_relData_t* data;
		data  = osmdb_relMembers_data(bm->rel_members);
		count = bm->rel_members->count;
		for(i = 0; i < count; ++i)
		{
			if(data[i].type == OSMDB_RELDATA_TYPE_NODE)
			{
				if(osmdb_index_gatherNode(self, tid, data[i].ref,
				                          map_export, os) == 0)
				{
					goto fail_member;
				}
			}
			else if(data[i].type == OSMDB_RELDATA_TYPE_WAY)
			{
				if(osmdb_index_gatherMemberWay(self, tid, data[i].ref,
				                               zoom, min_dist,
				                               map_export, os) == 0)
				{
					goto fail_member;
				}
			}
		}
	}

	if(osmdb_index_exportRel(self, bi->rel_info,
	                         bm ? bm->rel_members : NULL,
	                         br->rel_range, os) == 0)
	{
		goto fail_export;
	}

	// mark the relation as found
	if(cc_map_addf(map_export, (const void*) &OSMDB_ONE,
	               "r%" PRId64, rid) == 0)
	{
		goto fail_mark;
	}

	osmdb_index_put(self, &br);
	osmdb_index_put(self, &bm);
	osmdb_index_put(self, &bi);

	// success
	return 1;

	// failure
	fail_mark:
	fail_export:
	fail_member:
		osmdb_index_put(self, &br);
	fail_range:
		osmdb_index_put(self, &bm);
	fail_members:
		osmdb_index_put(self, &bi);
	return 0;
}

static int
osmdb_index_gatherNodes(osmdb_index_t* self,
                        int tid, int zoom, int x, int y,
                        cc_map_t* map_export,
                        xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	int     type;
	int64_t id;
	if(zoom == 14)
	{
		type = OSMDB_TYPE_TILEREF_NODE14;
		id   = 16384*y + x; // 2^14
	}
	else if(zoom == 11)
	{
		type = OSMDB_TYPE_TILEREF_NODE11;
		id   = 2048*y + x; // 2^11
	}
	else
	{
		LOGE("invalid zoom=%i", zoom);
		return 0;
	}

	osmdb_handle_t* hnd;
	if(osmdb_index_get(self, tid, type, id, &hnd) == 0)
	{
		return 0;
	}

	// check for empty tile
	if(hnd == NULL)
	{
		return 1;
	}

	int      i;
	int      ret   = 1;
	int      count = hnd->tile_refs->count;
	int64_t* refs  = osmdb_tileRefs_refs(hnd->tile_refs);
	for(i = 0; i < count; ++i)
	{
		if(osmdb_index_gatherNode(self, tid, refs[i],
		                          map_export, os) == 0)
		{
			ret = 0;
			break;
		}
	}

	osmdb_index_put(self, &hnd);

	return ret;
}

static int
osmdb_index_gatherRels(osmdb_index_t* self,
                       int tid, int zoom, int x, int y,
                       float min_dist,
                       cc_map_t* map_export,
                       xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	int     type;
	int64_t id;
	if(zoom == 14)
	{
		type = OSMDB_TYPE_TILEREF_REL14;
		id   = 16384*y + x; // 2^14
	}
	else if(zoom == 11)
	{
		type = OSMDB_TYPE_TILEREF_REL11;
		id   = 2048*y + x; // 2^11
	}
	else
	{
		LOGE("invalid zoom=%i", zoom);
		return 0;
	}

	osmdb_handle_t* hnd;
	if(osmdb_index_get(self, tid, type, id, &hnd) == 0)
	{
		return 0;
	}

	// check for empty tile
	if(hnd == NULL)
	{
		return 1;
	}

	int      i;
	int      ret   = 1;
	int      count = hnd->tile_refs->count;
	int64_t* refs  = osmdb_tileRefs_refs(hnd->tile_refs);
	for(i = 0; i < count; ++i)
	{
		if(osmdb_index_gatherRel(self, tid, refs[i], zoom,
		                         min_dist, map_export, os) == 0)
		{
			ret = 0;
			break;
		}
	}

	osmdb_index_put(self, &hnd);

	return ret;
}

static int
osmdb_index_joinWay(osmdb_index_t* self, int tid,
                    osmdb_segment_t* a, osmdb_segment_t* b,
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
	osmdb_handle_t* hnd_coord0 = NULL;
	osmdb_handle_t* hnd_coord1 = NULL;
	osmdb_handle_t* hnd_coord2 = NULL;
	if((osmdb_index_get(self, tid, OSMDB_TYPE_NODECOORD,
	                    *refp, &hnd_coord0) == 0) ||
	   (osmdb_index_get(self, tid, OSMDB_TYPE_NODECOORD,
	                    ref1, &hnd_coord1) == 0) ||
	   (osmdb_index_get(self, tid, OSMDB_TYPE_NODECOORD,
	                    *refn, &hnd_coord2) == 0) ||
	   (hnd_coord0 == NULL) || (hnd_coord1 == NULL) ||
	   (hnd_coord2 == NULL))
	{
		osmdb_index_put(self, &hnd_coord0);
		osmdb_index_put(self, &hnd_coord1);
		osmdb_index_put(self, &hnd_coord2);
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
	osmdb_nodeCoord_t* nc0 = hnd_coord0->node_coord;
	osmdb_nodeCoord_t* nc1 = hnd_coord1->node_coord;
	osmdb_nodeCoord_t* nc2 = hnd_coord2->node_coord;
	float onemi = cc_mi2m(5280.0f);
	terrain_geo2xyz(nc0->lat, nc0->lon, onemi,
	                &p0.x,  &p0.y, &p0.z);
	terrain_geo2xyz(nc1->lat, nc1->lon, onemi,
	                &p1.x,  &p1.y, &p1.z);
	terrain_geo2xyz(nc2->lat, nc2->lon, onemi,
	                &p2.x,  &p2.y, &p2.z);
	osmdb_index_put(self, &hnd_coord0);
	osmdb_index_put(self, &hnd_coord1);
	osmdb_index_put(self, &hnd_coord2);
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
	osmdb_wayInfo_t* ai = a->hnd_info->way_info;
	osmdb_wayInfo_t* bi = b->hnd_info->way_info;
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
osmdb_index_joinWays(osmdb_index_t* self,
                     int tid, cc_map_t* map_segs,
                     cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(map_segs);
	ASSERT(mm_nds_join);

	osmdb_segment_t*   seg1;
	osmdb_segment_t*   seg2;
	cc_multimapIter_t  miterator1;
	cc_multimapIter_t* miter1;
	cc_multimapIter_t  miterator2;
	cc_mapIter_t       hiter1;
	cc_mapIter_t       hiterator2;
	cc_mapIter_t*      hiter2;
	cc_listIter_t*     iter1;
	cc_listIter_t*     iter2;
	cc_list_t*         list1;
	cc_list_t*         list2;
	int64_t*           id1;
	int64_t*           id2;
	int64_t            ref1;
	int64_t            ref2;
	miter1 = cc_multimap_head(mm_nds_join, &miterator1);
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

			seg1 = (osmdb_segment_t*)
			       cc_map_findf(map_segs, &hiter1, "%" PRId64,
			                    *id1);
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

				seg2 = (osmdb_segment_t*)
				       cc_map_findf(map_segs, hiter2, "%" PRId64,
				                    *id2);
				if(seg2 == NULL)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				if(osmdb_index_joinWay(self, tid, seg1, seg2,
				                       ref1, &ref2) == 0)
				{
					iter2 = cc_list_next(iter2);
					continue;
				}

				// replace ref2->id2 with ref2->id1 in
				// mm_nds_join
				list2 = (cc_list_t*)
				        cc_multimap_findf(mm_nds_join, &miterator2,
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
				cc_map_remove(map_segs, &hiter2);
				osmdb_index_deleteSegment(self, &seg2);
				iter2 = NULL;
			}

			iter1 = cc_list_next(iter1);
		}

		miter1 = cc_multimap_nextList(miter1);
	}

	return 1;
}

static int
osmdb_index_sampleWays(osmdb_index_t* self, int tid,
                       int zoom, float min_dist,
                       cc_map_t* map_segs)
{
	ASSERT(self);
	ASSERT(map_segs);

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(map_segs, &miterator);
	while(miter)
	{
		osmdb_segment_t* seg;
		seg = (osmdb_segment_t*) cc_map_val(miter);
		if(osmdb_index_sampleWay(self, tid, zoom, min_dist,
		                         1, seg->list_nds) == 0)
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

static void
osmdb_index_clipWay(osmdb_index_t* self, int tid,
                    osmdb_segment_t* seg,
                    double latT, double lonL,
                    double latB, double lonR)
{
	ASSERT(self);
	ASSERT(seg);

	// don't clip short segs
	if(cc_list_size(seg->list_nds) <= 2)
	{
		return;
	}

	// check if way forms a loop
	int64_t* first = (int64_t*) cc_list_peekHead(seg->list_nds);
	int64_t* last  = (int64_t*) cc_list_peekTail(seg->list_nds);
	int     loop  = 0;
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
	int64_t*        ref;
	osmdb_handle_t* hnd_coord = NULL;
	cc_listIter_t*  iter;
	cc_listIter_t*  prev = NULL;
	iter = cc_list_head(seg->list_nds);
	while(iter)
	{
		ref = (int64_t*) cc_list_peekIter(iter);

		if((osmdb_index_get(self, tid, OSMDB_TYPE_NODECOORD,
		                    *ref, &hnd_coord) == 0) ||
		   (hnd_coord == NULL))
		{
			// ignore
			iter = cc_list_next(iter);
			continue;
		}

		// check if node is clipped
		osmdb_nodeCoord_t* node_coord;
		node_coord = hnd_coord->node_coord;
		if((node_coord->lat < latB) ||
		   (node_coord->lat > latT) ||
		   (node_coord->lon > lonR) ||
		   (node_coord->lon < lonL))
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
			osmdb_index_put(self, &hnd_coord);
			continue;
		}

		// compute the quadrant
		double pc[2] =
		{
			(node_coord->lon - center[0])/dlon,
			(node_coord->lat - center[1])/dlat
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
			osmdb_index_put(self, &hnd_coord);
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
			ref = (int64_t*) cc_list_remove(seg->list_nds, &prev);
			FREE(ref);
		}

		// clip last node
		if(clip_last)
		{
			ref = (int64_t*) cc_list_remove(seg->list_nds, &iter);
			FREE(ref);
			osmdb_index_put(self, &hnd_coord);
			return;
		}

		q0   = q1;
		q1   = q2;
		prev = iter;
		iter = cc_list_next(iter);
		osmdb_index_put(self, &hnd_coord);
	}
}

static int
osmdb_index_clipWays(osmdb_index_t* self, int tid,
                     double latT, double lonL,
                     double latB, double lonR,
                     cc_map_t* map_segs)
{
	ASSERT(self);
	ASSERT(map_segs);

	// elements are defined with zero width but in
	// practice are drawn with non-zero width
	// points/lines so an offset is needed to ensure they
	// are not clipped between neighboring tiles
	double dlat = (latT - latB)/16.0;
	double dlon = (lonR - lonL)/16.0;
	latT += dlat;
	latB -= dlat;
	lonL -= dlon;
	lonR += dlon;

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(map_segs, &miterator);
	while(miter)
	{
		osmdb_segment_t* seg;
		seg = (osmdb_segment_t*) cc_map_val(miter);

		osmdb_index_clipWay(self, tid, seg,
		                    latT, lonL, latB, lonR);

		miter = cc_map_next(miter);
	}

	return 1;
}

static int
osmdb_index_exportWays(osmdb_index_t* self,
                       int tid,
                       xml_ostream_t* os,
                       cc_map_t* map_segs,
                       cc_map_t* map_export)
{
	ASSERT(self);
	ASSERT(os);
	ASSERT(map_segs);
	ASSERT(map_export);

	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(map_segs, &miterator);
	while(miter)
	{
		osmdb_segment_t* seg;
		seg = (osmdb_segment_t*) cc_map_val(miter);

		// gather nds
		cc_listIter_t* iter = cc_list_head(seg->list_nds);
		while(iter)
		{
			int64_t* ref = (int64_t*) cc_list_peekIter(iter);
			if(osmdb_index_gatherNode(self, tid, *ref,
			                          map_export, os) == 0)
			{
				return 0;
			}
			iter = cc_list_next(iter);
		}

		osmdb_wayInfo_t*  way_info;
		osmdb_wayRange_t* way_range;
		way_info  = seg->hnd_info->way_info;
		way_range = &seg->way_range;
		if(osmdb_index_exportWay(self, way_info, way_range,
		                         seg->list_nds, os) == 0)
		{
			return 0;
		}

		miter = cc_map_next(miter);
	}

	return 1;
}

static int
osmdb_index_gatherWay(osmdb_index_t* self,
                      int tid,
                      int64_t wid,
                      cc_map_t* map_export,
                      cc_map_t* map_segs,
                      cc_multimap_t* mm_nds_join)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(map_segs);
	ASSERT(mm_nds_join);

	// check if way is already included
	cc_mapIter_t miterator;
	if(cc_map_findf(map_export, &miterator,
	                "w%" PRId64, wid))
	{
		return 1;
	}

	// create segment
	osmdb_segment_t* seg = NULL;
	if(osmdb_index_newSegment(self, tid, wid, &seg) == 0)
	{
		return 0;
	}
	else if(seg == NULL)
	{
		// way may not exist due to osmosis
		return 1;
	}

	if(cc_map_addf(map_segs, (const void*) seg,
	               "%" PRId64, wid) == 0)
	{
		osmdb_index_deleteSegment(self, &seg);
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

	if(cc_multimap_addf(mm_nds_join, (const void*) id1_copy,
	                    "%" PRId64, *ref1) == 0)
	{
		FREE(id1_copy);
		FREE(id2_copy);
		return 0;
	}

	if(cc_multimap_addf(mm_nds_join, (const void*) id2_copy,
	                    "%" PRId64, *ref2) == 0)
	{
		FREE(id2_copy);
		return 0;
	}

	return 1;
}

static int
osmdb_index_gatherWays(osmdb_index_t* self,
                       int tid, int zoom, int x, int y,
                       float min_dist,
                       double latT, double lonL,
                       double latB, double lonR,
                       cc_map_t* map_export,
                       xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(map_export);
	ASSERT(os);

	int ret = 0;

	int     type;
	int64_t id;
	if(zoom == 14)
	{
		type = OSMDB_TYPE_TILEREF_WAY14;
		id   = 16384*y + x; // 2^14
	}
	else if(zoom == 11)
	{
		type = OSMDB_TYPE_TILEREF_WAY11;
		id   = 2048*y + x; // 2^11
	}
	else
	{
		LOGE("invalid zoom=%i", zoom);
		return 0;
	}

	osmdb_handle_t* hnd;
	if(osmdb_index_get(self, tid, type, id, &hnd) == 0)
	{
		return 0;
	}

	// check for empty tile
	if(hnd == NULL)
	{
		return 1;
	}

	cc_map_t* map_segs = cc_map_new();
	if(map_segs == NULL)
	{
		goto fail_map_segs;
	}

	cc_multimap_t* mm_nds_join = cc_multimap_new(NULL);
	if(mm_nds_join == NULL)
	{
		goto fail_mm_nds_join;
	}

	int      i;
	int      count = hnd->tile_refs->count;
	int64_t* refs  = osmdb_tileRefs_refs(hnd->tile_refs);
	for(i = 0; i < count; ++i)
	{
		if(osmdb_index_gatherWay(self, tid, refs[i],
		                         map_export, map_segs,
		                         mm_nds_join) == 0)
		{
			goto fail_gather_way;
		}
	}

	if(osmdb_index_joinWays(self, tid, map_segs,
	                        mm_nds_join) == 0)
	{
		goto fail_join;
	}

	if(osmdb_index_sampleWays(self, tid, zoom, min_dist,
	                          map_segs) == 0)
	{
		goto fail_sample;
	}

	if(osmdb_index_clipWays(self, tid,
	                        latT, lonL, latB, lonR,
	                        map_segs) == 0)
	{
		goto fail_clip;
	}

	if(osmdb_index_exportWays(self, tid, os, map_segs,
	                          map_export) == 0)
	{
		goto fail_export;
	}

	// success
	ret = 1;

	// success or failure
	fail_export:
	fail_clip:
	fail_sample:
	fail_join:
	fail_gather_way:
	{
		cc_multimapIter_t  miterator;
		cc_multimapIter_t* miter;
		miter = cc_multimap_head(mm_nds_join, &miterator);
		while(miter)
		{
			int64_t* ref;
			ref = (int64_t*)
			      cc_multimap_remove(mm_nds_join, &miter);
			FREE(ref);
		}
		cc_multimap_delete(&mm_nds_join);
	}
	fail_mm_nds_join:
	{
		cc_mapIter_t  miterator;
		cc_mapIter_t* miter = cc_map_head(map_segs, &miterator);
		while(miter)
		{
			osmdb_segment_t* seg;
			seg = (osmdb_segment_t*)
			      cc_map_remove(map_segs, &miter);
			osmdb_index_deleteSegment(self, &seg);
		}
		cc_map_delete(&map_segs);
	}
	fail_map_segs:
		osmdb_index_put(self, &hnd);
	return ret;
}

/***********************************************************
* protected - importer                                     *
***********************************************************/

int osmdb_index_updateChangeset(osmdb_index_t* self,
                                int64_t changeset)
{
	ASSERT(self);

	char sql[256];
	snprintf(sql, 256,
	         "REPLACE INTO tbl_attr (key, val)"
	         "	VALUES ('changeset', '%" PRId64 "');",
	         changeset);

	int ret = 1;
	if(sqlite3_exec(self->db, sql, NULL, NULL,
	                NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_exec: %s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	return ret;
}

int osmdb_index_add(osmdb_index_t* self,
                    int type, int64_t id,
                    size_t size,
                    void* data)
{
	ASSERT(self);

	osmdb_entry_t* entry;

	int64_t major_id = id/OSMDB_ENTRY_SIZE;
	if(type < OSMDB_TYPE_TILEREF_COUNT)
	{
		major_id = id;
	}

	// check if entry is in cache
	cc_mapIter_t   miterator;
	cc_listIter_t* iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->cache_map, &miterator,
	                    "%i/%" PRId64, type, major_id);
	if(iter)
	{
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);

		if(osmdb_entry_add(entry, 0, size, data) == 0)
		{
			return 0;
		}

		// update LRU cache
		cc_list_moven(self->cache_list, iter, NULL);

		osmdb_index_trim(self);
		return 1;
	}

	// otherwise create a new entry
	entry = osmdb_entry_new(type, major_id);
	if(entry == NULL)
	{
		return 0;
	}

	if(osmdb_index_load(self, 0, entry) == 0)
	{
		goto fail_load;
	}

	iter = cc_list_append(self->cache_list, NULL,
	                      (const void*) entry);
	if(iter == NULL)
	{
		goto fail_append;
	}

	if(cc_map_addf(self->cache_map, (const void*) iter,
	               "%i/%" PRId64, type, major_id) == 0)
	{
		goto fail_add;
	}

	if(osmdb_entry_add(entry, 0, size, data) == 0)
	{
		// fail w/o removing entry from cache
		return 0;
	}

	osmdb_index_trim(self);

	// success
	return 1;

	// failure
	fail_add:
		cc_list_remove(self->cache_list, &iter);
	fail_append:
	fail_load:
		osmdb_entry_delete(&entry);
	return 0;
}

int osmdb_index_addTile(osmdb_index_t* self, int type,
                        int64_t major_id, int64_t ref)
{
	ASSERT(self);
	ASSERT(type < OSMDB_TYPE_TILEREF_COUNT);

	osmdb_entry_t* entry;

	// check if entry is in cache
	osmdb_tileRefs_t* tile_refs;
	cc_mapIter_t      miterator;
	cc_listIter_t*    iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->cache_map, &miterator,
	                    "%i/%" PRId64, type, major_id);
	if(iter)
	{
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);

		if(osmdb_entry_add(entry, 0, sizeof(int64_t),
		                   (void*) &ref) == 0)
		{
			return 0;
		}

		// update tile count
		tile_refs = (osmdb_tileRefs_t*) entry->data;
		++tile_refs->count;

		// update LRU cache
		cc_list_moven(self->cache_list, iter, NULL);
		osmdb_index_trim(self);

		return 1;
	}

	// otherwise create a new entry
	entry = osmdb_entry_new(type, major_id);
	if(entry == NULL)
	{
		return 0;
	}

	if(osmdb_index_load(self, 0, entry) == 0)
	{
		goto fail_load;
	}

	iter = cc_list_append(self->cache_list, NULL,
	                      (const void*) entry);
	if(iter == NULL)
	{
		goto fail_append;
	}

	if(cc_map_addf(self->cache_map, (const void*) iter,
	               "%i/%" PRId64, type, major_id) == 0)
	{
		goto fail_add;
	}

	// add tile header if not already loaded
	if(entry->data == NULL)
	{
		osmdb_tileRefs_t tmp =
		{
			.id    = major_id,
			.count = 0
		};

		if(osmdb_entry_add(entry, 0, sizeof(osmdb_tileRefs_t),
		                   (void*) &tmp) == 0)
		{
			// fail w/o removing entry from cache
			return 0;
		}
	}

	if(osmdb_entry_add(entry, 0, sizeof(int64_t),
	                   (void*) &ref) == 0)
	{
		// fail w/o removing entry from cache
		return 0;
	}

	// update tile count
	tile_refs = (osmdb_tileRefs_t*) entry->data;
	++tile_refs->count;

	osmdb_index_trim(self);

	// success
	return 1;

	// failure
	fail_add:
		cc_list_remove(self->cache_list, &iter);
	fail_append:
	fail_load:
		osmdb_entry_delete(&entry);
	return 0;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_index_t*
osmdb_index_new(const char* fname, int mode, int nth)
{
	ASSERT(fname);

	// validate thread count
	if((nth <= 0) ||
	   ((nth > 1) && (mode != OSMDB_INDEX_MODE_READONLY)))
	{
		LOGE("invalid mode=%i, nth=%i", nth);
		return NULL;
	}

	osmdb_index_t* self;
	self = (osmdb_index_t*)
	       CALLOC(1, sizeof(osmdb_index_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->mode = mode;
	self->nth  = nth;

	if(sqlite3_initialize() != SQLITE_OK)
	{
		LOGE("sqlite3_initialize failed");
		goto fail_init;
	}

	int flags = SQLITE_OPEN_READONLY;
	if(mode == OSMDB_INDEX_MODE_CREATE)
	{
		flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
	}
	else if(mode == OSMDB_INDEX_MODE_APPEND)
	{
		flags = SQLITE_OPEN_READWRITE;
	}

	if(sqlite3_open_v2(fname, &self->db,
	                   flags, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 %s failed", fname);
		goto fail_open;
	}

	if(mode == OSMDB_INDEX_MODE_CREATE)
	{
		if(osmdb_index_createTables(self) == 0)
		{
			goto fail_create;
		}
	}

	const char* sql_begin = "BEGIN;";
	if(sqlite3_prepare_v2(self->db, sql_begin, -1,
	                      &self->stmt_begin,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_begin;
	}

	const char* sql_end = "END;";
	if(sqlite3_prepare_v2(self->db, sql_end, -1,
	                      &self->stmt_end,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_end;
	}

	const char* sql_changeset;
	sql_changeset = "SELECT val FROM tbl_attr WHERE "
	                "key='changeset';";
	if(sqlite3_prepare_v2(self->db, sql_changeset, -1,
	                      &self->stmt_changeset,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_changeset;
	}

	if(osmdb_index_prepareInsert(self) == 0)
	{
		goto fail_prepare_insert;
	}

	if(osmdb_index_prepareSelect(self) == 0)
	{
		goto fail_prepare_select;
	}

	if(pthread_mutex_init(&self->cache_mutex, NULL) != 0)
	{
		LOGE("pthread_mutex_init failed");
		goto fail_cache_mutex;
	}

	if(pthread_cond_init(&self->cache_cond, NULL) != 0)
	{
		LOGE("pthread_cond_init failed");
		goto fail_cache_cond;
	}

	self->cache_map = cc_map_new();
	if(self->cache_map == NULL)
	{
		goto fail_cache_map;
	}

	self->cache_list = cc_list_new();
	if(self->cache_list == NULL)
	{
		goto fail_cache_list;
	}

	self->cache_loading = (osmdb_cacheLoading_t*)
	                     CALLOC(nth, sizeof(osmdb_cacheLoading_t));
	if(self->cache_loading == NULL)
	{
		goto fail_cache_loading;
	}

	// initialize loading
	int i;
	for(i = 0; i < nth; ++i)
	{
		self->cache_loading[i].type = -1;
		self->cache_loading[i].id   = -1;
	}

	// success
	return self;

	// failure
	fail_cache_loading:
		cc_list_delete(&self->cache_list);
	fail_cache_list:
		cc_map_delete(&self->cache_map);
	fail_cache_map:
		pthread_cond_destroy(&self->cache_cond);
	fail_cache_cond:
		pthread_mutex_destroy(&self->cache_mutex);
	fail_cache_mutex:
		osmdb_index_finalizeSelect(self);
	fail_prepare_select:
		osmdb_index_finalizeInsert(self);
	fail_prepare_insert:
		sqlite3_finalize(self->stmt_changeset);
	fail_prepare_changeset:
		sqlite3_finalize(self->stmt_end);
	fail_prepare_end:
		sqlite3_finalize(self->stmt_begin);
	fail_prepare_begin:
		fail_create:
	fail_open:
	{
		// close db even when open fails
		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(sqlite3_shutdown() != SQLITE_OK)
		{
			LOGW("sqlite3_shutdown failed");
		}
	}
	fail_init:
		FREE(self);
	return NULL;
}

void osmdb_index_delete(osmdb_index_t** _self)
{
	ASSERT(_self);

	osmdb_index_t* self = *_self;
	if(self)
	{
		// empty cache
		double         t0 = cc_timestamp();
		double         t1 = t0;
		cc_mapIter_t   miterator;
		cc_mapIter_t*  miter;
		cc_listIter_t* iter;
		osmdb_entry_t* entry;
		miter = cc_map_head(self->cache_map, &miterator);
		while(miter)
		{
			double t2 = cc_timestamp();
			if(t2 - t1 > 10.0)
			{
				LOGI("dt=%0.0lf, entries=%i",
				     t2 - t0, cc_map_size(self->cache_map));
				t1 = t2;
			}

			iter  = (cc_listIter_t*)
			        cc_map_remove(self->cache_map, &miter);
			entry = (osmdb_entry_t*)
			        cc_list_remove(self->cache_list, &iter);
			osmdb_index_evict(self, &entry);
		}

		if(osmdb_index_endTransaction(self) == 0)
		{
			// ignore
		}

		FREE(self->cache_loading);
		cc_list_delete(&self->cache_list);
		cc_map_delete(&self->cache_map);
		pthread_cond_destroy(&self->cache_cond);
		pthread_mutex_destroy(&self->cache_mutex);
		osmdb_index_finalizeSelect(self);
		osmdb_index_finalizeInsert(self);
		sqlite3_finalize(self->stmt_changeset);
		sqlite3_finalize(self->stmt_end);
		sqlite3_finalize(self->stmt_begin);

		// close db even when open fails
		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(sqlite3_shutdown() != SQLITE_OK)
		{
			LOGW("sqlite3_shutdown failed");
		}

		FREE(self);
		*_self = NULL;
	}
}

int64_t osmdb_index_changeset(osmdb_index_t* self)
{
	ASSERT(self);

	int64_t changeset = 0;

	sqlite3_stmt* stmt = self->stmt_changeset;

	osmdb_index_lock(self);

	if(sqlite3_step(stmt) == SQLITE_ROW)
	{
		const unsigned char* val;
		val = sqlite3_column_text(stmt, 0);
		changeset = (int64_t)
		            strtoll((const char*) val, NULL, 0);
	}
	else
	{
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	osmdb_index_unlock(self, 0);

	return changeset;
}

int osmdb_index_get(osmdb_index_t* self,
                    int tid, int type, int64_t id,
                    osmdb_handle_t** _hnd)
{
	ASSERT(self);
	ASSERT(_hnd);

	int64_t major_id = id/OSMDB_ENTRY_SIZE;
	int64_t minor_id = id%OSMDB_ENTRY_SIZE;
	if(type < OSMDB_TYPE_TILEREF_COUNT)
	{
		major_id = id;
		minor_id = 0;
	}

	osmdb_index_lockRead(self);

	// find the entry in the cache
	// note that it is not an error to return a NULL hnd
	osmdb_entry_t* entry;
	cc_mapIter_t   miterator;
	cc_listIter_t* iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->cache_map, &miterator,
	                    "%i/%" PRId64, type, major_id);
	if(iter)
	{
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);

		osmdb_index_lockReadUpdate(self);

		// get hnd if it exists
		int ret = 1;
		if(osmdb_entry_get(entry, minor_id, _hnd) == 0)
		{
			ret = 0;
		}
		else
		{
			// update LRU cache
			cc_list_moven(self->cache_list, iter, NULL);
		}

		if(self->cache_editor && (self->cache_readers == 0))
		{
			osmdb_index_unlock(self, 1);
		}
		else
		{
			osmdb_index_unlock(self, 0);
		}

		return ret;
	}

	osmdb_index_lockLoad(self, tid, type, id);

	// retry find after locking for load since the entry
	// could have been loaded in parallel by another thread
	iter = (cc_listIter_t*)
	       cc_map_findf(self->cache_map, &miterator,
	                    "%i/%" PRId64, type, major_id);
	if(iter)
	{
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);

		osmdb_index_lockLoadUpdate(self, tid);

		// get hnd if it exists
		int ret = 1;
		if(osmdb_entry_get(entry, minor_id, _hnd) == 0)
		{
			ret = 0;
		}
		else
		{
			// update LRU cache
			cc_list_moven(self->cache_list, iter, NULL);
		}

		osmdb_index_unlock(self, 1);

		return ret;
	}

	entry = osmdb_entry_new(type, major_id);
	if(entry == NULL)
	{
		goto fail_entry;
	}

	if(osmdb_index_load(self, tid, entry) == 0)
	{
		goto fail_load;
	}

	if(osmdb_entry_get(entry, minor_id, _hnd) == 0)
	{
		goto fail_get;
	}

	osmdb_index_lockEdit(self, tid);

	if(osmdb_index_trim(self) == 0)
	{
		goto fail_trim;
	}

	iter = cc_list_append(self->cache_list, NULL,
	                      (const void*) entry);
	if(iter == NULL)
	{
		goto fail_append;
	}

	if(cc_map_addf(self->cache_map, (const void*) iter,
	               "%i/%" PRId64, type, major_id) == 0)
	{
		goto fail_add;
	}

	osmdb_index_unlock(self, 1);

	// success
	return 1;

	// failure with load lock
	fail_get:
	fail_load:
		osmdb_entry_delete(&entry);
	fail_entry:
		osmdb_index_unlockLoadErr(self, tid);
	return 0;

	// failure with edit lock
	fail_add:
		cc_list_remove(self->cache_list, &iter);
	fail_append:
	fail_trim:
		osmdb_index_unlock(self, 1);
		osmdb_entry_put(entry, _hnd);
		osmdb_entry_delete(&entry);
	return 0;
}

void osmdb_index_put(osmdb_index_t* self,
                     osmdb_handle_t** _hnd)
{
	ASSERT(self);
	ASSERT(_hnd);

	osmdb_handle_t* hnd = *_hnd;
	if(hnd)
	{
		osmdb_index_lock(self);

		osmdb_entry_t* entry = hnd->entry;
		osmdb_entry_put(entry, _hnd);

		osmdb_index_unlock(self, 0);
	}
}

int osmdb_index_tile(osmdb_index_t* self,
                     int tid,
                     int zoom, int x, int y,
                     xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(os);

	cc_map_t* map_export = cc_map_new();
	if(map_export == NULL)
	{
		return 0;
	}

	double latT;
	double lonL;
	double latB;
	double lonR;
	terrain_bounds(x, y, zoom, &latT, &lonL, &latB, &lonR);

	// compute x,y for tile
	cc_vec3f_t pa;
	cc_vec3f_t pb;
	float onemi = cc_mi2m(5280.0f);
	terrain_geo2xyz(latT, lonL, onemi,
	                &pa.x, &pa.y, &pa.z);
	terrain_geo2xyz(latB, lonR, onemi,
	                &pb.x, &pb.y, &pb.z);

	// compute min_dist
	// scale by 1/8th since each tile serves 3 zoom levels
	float s        = 1.0f/8.0f;
	float pix      = sqrtf(2*256.0f*256.0f);
	float min_dist = s*cc_vec3f_distance(&pb, &pa)/pix;

	xml_ostream_begin(os, "osmdb");
	if(osmdb_index_gatherNodes(self, tid, zoom, x, y,
	                           map_export, os) == 0)
	{
		goto fail_gather_nodes;
	}

	if(osmdb_index_gatherRels(self, tid, zoom, x, y, min_dist,
	                          map_export, os) == 0)
	{
		goto fail_gather_relations;
	}

	if(osmdb_index_gatherWays(self, tid, zoom, x, y, min_dist,
	                          latT, lonL, latB, lonR,
	                          map_export, os) == 0)
	{
		goto fail_gather_ways;
	}

	xml_ostream_end(os);

	cc_map_discard(map_export);
	cc_map_delete(&map_export);

	// success
	return 1;

	// failure
	fail_gather_ways:
	fail_gather_relations:
	fail_gather_nodes:
		cc_map_discard(map_export);
		cc_map_delete(&map_export);
	return 0;
}

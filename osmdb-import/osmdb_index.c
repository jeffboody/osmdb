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
#include <stdio.h>
#include <stdlib.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_timestamp.h"
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

/***********************************************************
* private                                                  *
***********************************************************/

static void
osmdb_index_lock(osmdb_index_t* self)
{
	ASSERT(self);

	#ifndef OSMDB_IMPORTER
	pthread_mutex_lock(&self->cache_mutex);
	#endif
}

static void
osmdb_index_unlock(osmdb_index_t* self)
{
	ASSERT(self);

	#ifndef OSMDB_IMPORTER
	pthread_mutex_unlock(&self->cache_mutex);
	#endif
}

int osmdb_index_createTables(osmdb_index_t* self)
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

	for(i = 0; i < OSMDB_BLOB_TYPE_COUNT; ++i)
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

	#ifdef OSMDB_IMPORTER
		if(self->batch_size == 0)
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
	#endif

	return ret;
}

static int
osmdb_index_beginTransaction(osmdb_index_t* self)
{
	ASSERT(self);

	int ret = 1;

	#ifdef OSMDB_IMPORTER
		if(self->batch_size >= OSMDB_INDEX_BATCH_SIZE)
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
	#endif

	return ret;
}

static int
osmdb_index_load(osmdb_index_t* self,
                 osmdb_entry_t* entry)
{
	ASSERT(self);
	ASSERT(entry);

	int idx_id;
	sqlite3_stmt* stmt;

	stmt   = self->stmt_select[entry->type];
	idx_id = self->idx_select_id[entry->type];

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

static void
osmdb_index_finalizeInsert(osmdb_index_t* self)
{
	ASSERT(self);

	int i;
	for(i = 0; i < OSMDB_BLOB_TYPE_COUNT; ++i)
	{
		sqlite3_finalize(self->stmt_insert[i]);
		self->stmt_insert[i] = NULL;
	}
}

static void
osmdb_index_finalizeSelect(osmdb_index_t* self)
{
	ASSERT(self);

	int i;
	for(i = 0; i < OSMDB_BLOB_TYPE_COUNT; ++i)
	{
		sqlite3_finalize(self->stmt_select[i]);
		self->stmt_select[i] = NULL;
	}
}

static int
osmdb_index_prepareInsert(osmdb_index_t* self)
{
	ASSERT(self);

	int i;
	for(i = 0; i < OSMDB_BLOB_TYPE_COUNT; ++i)
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

	int i;
	for(i = 0; i < OSMDB_BLOB_TYPE_COUNT; ++i)
	{
		char sql_select[256];
		snprintf(sql_select, 256,
		         "SELECT blob FROM %s WHERE id=@arg_id;",
		         OSMDB_INDEX_TBL[i]);

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
	}
	return 0;
}

/***********************************************************
* protected                                                *
***********************************************************/

#ifdef OSMDB_IMPORTER
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

	int64_t major_id = id/OSMDB_BLOB_SIZE;
	if(type < OSMDB_BLOB_TYPE_TILE_COUNT)
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

		int added = osmdb_entry_add(entry, 0, size, data);

		// update LRU cache
		cc_list_moven(self->cache_list, iter, NULL);

		osmdb_index_trim(self);
		return added;
	}

	// otherwise create a new entry
	entry = osmdb_entry_new(type, major_id);
	if(entry == NULL)
	{
		return 0;
	}

	if(osmdb_index_load(self, entry) == 0)
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

	int added = osmdb_entry_add(entry, 0, size, data);
	osmdb_index_trim(self);

	// success
	return added;

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
	ASSERT(type < OSMDB_BLOB_TYPE_TILE_COUNT);

	osmdb_entry_t* entry;

	// check if entry is in cache
	cc_mapIter_t   miterator;
	cc_listIter_t* iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->cache_map, &miterator,
	                    "%i/%" PRId64, type, major_id);
	if(iter)
	{
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);

		int added = osmdb_entry_add(entry, 0, sizeof(int64_t),
		                            (void*) &ref);

		// update LRU cache
		cc_list_moven(self->cache_list, iter, NULL);

		osmdb_index_trim(self);
		return added;
	}

	// otherwise create a new entry
	entry = osmdb_entry_new(type, major_id);
	if(entry == NULL)
	{
		return 0;
	}

	if(osmdb_index_load(self, entry) == 0)
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

	int added = osmdb_entry_add(entry, 0, sizeof(int64_t),
	                            (void*) &ref);
	osmdb_index_trim(self);

	// success
	return added;

	// failure
	fail_add:
		cc_list_remove(self->cache_list, &iter);
	fail_append:
	fail_load:
		osmdb_entry_delete(&entry);
	return 0;
}
#endif

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_index_t*
osmdb_index_new(const char* fname)
{
	ASSERT(fname);

	osmdb_index_t* self;
	self = (osmdb_index_t*)
	       CALLOC(1, sizeof(osmdb_index_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	if(sqlite3_initialize() != SQLITE_OK)
	{
		LOGE("sqlite3_initialize failed");
		goto fail_init;
	}

	#ifdef OSMDB_IMPORTER
		int flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
	#else
		int flags = SQLITE_OPEN_READONLY;
	#endif
	if(sqlite3_open_v2(fname, &self->db,
	                   flags, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 %s failed", fname);
		goto fail_open;
	}

	#ifdef OSMDB_IMPORTER
		if(osmdb_index_createTables(self) == 0)
		{
			goto fail_create;
		}
	#endif

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

	// success
	return self;

	// failure
	fail_cache_list:
		cc_map_delete(&self->cache_map);
	fail_cache_map:
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
	#ifdef OSMDB_IMPORTER
		fail_create:
	#endif
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

		cc_list_delete(&self->cache_list);
		cc_map_delete(&self->cache_map);
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

	return changeset;
}

int osmdb_index_get(osmdb_index_t* self,
                    int type, int64_t id,
                    osmdb_blob_t** _blob)
{
	ASSERT(self);
	ASSERT(_blob);

	int64_t major_id = id/OSMDB_BLOB_SIZE;
	int64_t minor_id = id%OSMDB_BLOB_SIZE;
	if(type < OSMDB_BLOB_TYPE_TILE_COUNT)
	{
		major_id = id;
		minor_id = 0;
	}

	osmdb_index_lock(self);

	// check if entry is in cache
	// note that it is not an error to return a NULL blob
	osmdb_entry_t* entry;
	cc_mapIter_t   miterator;
	cc_listIter_t* iter;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->cache_map, &miterator,
	                    "%i/%" PRId64, type, major_id);
	if(iter)
	{
		entry = (osmdb_entry_t*) cc_list_peekIter(iter);

		// get blob if it exists
		int ret = 1;
		if(osmdb_entry_get(entry, minor_id, _blob) == 0)
		{
			ret = 0;
		}

		// update LRU cache
		cc_list_moven(self->cache_list, iter, NULL);

		osmdb_index_unlock(self);
		return ret;
	}

	entry = osmdb_entry_new(type, major_id);
	if(entry == NULL)
	{
		goto fail_create;
	}

	if(osmdb_index_load(self, entry) == 0)
	{
		goto fail_load;
	}

	if(osmdb_entry_get(entry, minor_id, _blob) == 0)
	{
		goto fail_get;
	}

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

	osmdb_index_unlock(self);

	// success
	return 1;

	// failure
	fail_add:
		cc_list_remove(self->cache_list, &iter);
	fail_append:
	fail_trim:
		osmdb_entry_put(entry, _blob);
	fail_get:
	fail_load:
		osmdb_entry_delete(&entry);
	fail_create:
		osmdb_index_unlock(self);
	return 0;
}

void osmdb_index_put(osmdb_index_t* self,
                     osmdb_blob_t** _blob)
{
	ASSERT(self);
	ASSERT(_blob);

	osmdb_blob_t* blob = *_blob;
	if(blob)
	{
		osmdb_index_lock(self);

		osmdb_entry_t* entry = (osmdb_entry_t*) blob->priv;
		osmdb_entry_put(entry, _blob);

		osmdb_index_unlock(self);
	}
}

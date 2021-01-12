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

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_timestamp.h"
#include "osmdb_entry.h"
#include "osmdb_index.h"

#define OSMDB_INDEX_MAXSIZE 4000000000

#define OSMDB_INDEX_BATCH_SIZE 10000

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

	const char* sql[] =
	{
		"PRAGMA journal_mode = OFF;",
		"PRAGMA locking_mode = EXCLUSIVE;",
		"PRAGMA temp_store_directory = '.';",
		"CREATE TABLE tbl_nodeCoord"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_nodeInfo"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_wayInfo"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_wayRange"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_wayNds"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_relInfo"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_relMembers"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		"CREATE TABLE tbl_relRange"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		NULL
	};

	int idx = 0;
	while(sql[idx])
	{
		if(sqlite3_exec(self->db, sql[idx], NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec(%i): %s",
			     idx, sqlite3_errmsg(self->db));
			return 0;
		}
		++idx;
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

	sqlite3_stmt* stmt   = NULL;
	int           idx_id = 0;
	if(entry->type == OSMDB_BLOB_TYPE_NODE_COORD)
	{
		stmt   = self->stmt_select_nodeCoord;
		idx_id = self->idx_select_nodeCoordId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_NODE_INFO)
	{
		stmt   = self->stmt_select_nodeInfo;
		idx_id = self->idx_select_nodeInfoId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_WAY_INFO)
	{
		stmt   = self->stmt_select_wayInfo;
		idx_id = self->idx_select_wayInfoId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_WAY_RANGE)
	{
		stmt   = self->stmt_select_wayRange;
		idx_id = self->idx_select_wayRangeId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_WAY_NDS)
	{
		stmt   = self->stmt_select_wayNds;
		idx_id = self->idx_select_wayNdsId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_REL_INFO)
	{
		stmt   = self->stmt_select_relInfo;
		idx_id = self->idx_select_relInfoId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_REL_MEMBERS)
	{
		stmt   = self->stmt_select_relMembers;
		idx_id = self->idx_select_relMembersId;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_REL_RANGE)
	{
		stmt   = self->stmt_select_relRange;
		idx_id = self->idx_select_relRangeId;
	}
	else
	{
		LOGE("invalid type=%i, major_id=%" PRId64,
		     entry->type, entry->major_id);
		return 0;
	}

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

	sqlite3_stmt* stmt     = NULL;
	int           idx_id   = 0;
	int           idx_blob = 0;
	if(entry->type == OSMDB_BLOB_TYPE_NODE_COORD)
	{
		stmt     = self->stmt_insert_nodeCoord;
		idx_id   = self->idx_insert_nodeCoordId;
		idx_blob = self->idx_insert_nodeCoordBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_NODE_INFO)
	{
		stmt     = self->stmt_insert_nodeInfo;
		idx_id   = self->idx_insert_nodeInfoId;
		idx_blob = self->idx_insert_nodeInfoBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_WAY_INFO)
	{
		stmt     = self->stmt_insert_wayInfo;
		idx_id   = self->idx_insert_wayInfoId;
		idx_blob = self->idx_insert_wayInfoBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_WAY_RANGE)
	{
		stmt     = self->stmt_insert_wayRange;
		idx_id   = self->idx_insert_wayRangeId;
		idx_blob = self->idx_insert_wayRangeBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_WAY_NDS)
	{
		stmt     = self->stmt_insert_wayNds;
		idx_id   = self->idx_insert_wayNdsId;
		idx_blob = self->idx_insert_wayNdsBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_REL_INFO)
	{
		stmt     = self->stmt_insert_relInfo;
		idx_id   = self->idx_insert_relInfoId;
		idx_blob = self->idx_insert_relInfoBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_REL_MEMBERS)
	{
		stmt     = self->stmt_insert_relMembers;
		idx_id   = self->idx_insert_relMembersId;
		idx_blob = self->idx_insert_relMembersBlob;
	}
	else if(entry->type == OSMDB_BLOB_TYPE_REL_RANGE)
	{
		stmt     = self->stmt_insert_relRange;
		idx_id   = self->idx_insert_relRangeId;
		idx_blob = self->idx_insert_relRangeBlob;
	}
	else
	{
		LOGE("invalid type=%i, major_id=%" PRId64,
		     entry->type, entry->major_id);
		return 0;
	}

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
			if(size <= OSMDB_INDEX_MAXSIZE)
			{
				break;
			}

			first = 0;
		}
		if(size <= (size_t) (0.95f*OSMDB_INDEX_MAXSIZE))
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
* protected                                                *
***********************************************************/

#ifdef OSMDB_IMPORTER
int osmdb_index_add(osmdb_index_t* self,
                    int type, int64_t id,
                    size_t size,
                    void* data)
{
	ASSERT(self);

	osmdb_entry_t* entry;

	int64_t major_id = id/OSMDB_BLOB_SIZE;

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

	const char* sql_insert_nodeCoord =
		"REPLACE INTO tbl_nodeCoord (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodeCoord, -1,
	                      &self->stmt_insert_nodeCoord,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodeCoord;
	}

	const char* sql_insert_nodeInfo =
		"REPLACE INTO tbl_nodeInfo (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodeInfo, -1,
	                      &self->stmt_insert_nodeInfo,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodeInfo;
	}

	const char* sql_insert_wayInfo =
		"REPLACE INTO tbl_wayInfo (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_wayInfo, -1,
	                      &self->stmt_insert_wayInfo,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_wayInfo;
	}

	const char* sql_insert_wayRange =
		"REPLACE INTO tbl_wayRange (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_wayRange, -1,
	                      &self->stmt_insert_wayRange,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_wayRange;
	}

	const char* sql_insert_wayNds =
		"REPLACE INTO tbl_wayNds (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_wayNds, -1,
	                      &self->stmt_insert_wayNds,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_wayNds;
	}

	const char* sql_insert_relInfo =
		"REPLACE INTO tbl_relInfo (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_relInfo, -1,
	                      &self->stmt_insert_relInfo,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_relInfo;
	}

	const char* sql_insert_relMembers =
		"REPLACE INTO tbl_relMembers (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_relMembers, -1,
	                      &self->stmt_insert_relMembers,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_relMembers;
	}

	const char* sql_insert_relRange =
		"REPLACE INTO tbl_relRange (id, blob)"
		"	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_insert_relRange, -1,
	                      &self->stmt_insert_relRange,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_relRange;
	}

	const char* sql_select_nodeCoord =
		"SELECT blob FROM tbl_nodeCoord WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_nodeCoord, -1,
	                      &self->stmt_select_nodeCoord,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_nodeCoord;
	}

	const char* sql_select_nodeInfo =
		"SELECT blob FROM tbl_nodeInfo WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_nodeInfo, -1,
	                      &self->stmt_select_nodeInfo,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_nodeInfo;
	}

	const char* sql_select_wayInfo =
		"SELECT blob FROM tbl_wayInfo WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_wayInfo, -1,
	                      &self->stmt_select_wayInfo,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_wayInfo;
	}

	const char* sql_select_wayRange =
		"SELECT blob FROM tbl_wayRange WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_wayRange, -1,
	                      &self->stmt_select_wayRange,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_wayRange;
	}

	const char* sql_select_wayNds =
		"SELECT blob FROM tbl_wayNds WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_wayNds, -1,
	                      &self->stmt_select_wayNds,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_wayNds;
	}

	const char* sql_select_relInfo =
		"SELECT blob FROM tbl_relInfo WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_relInfo, -1,
	                      &self->stmt_select_relInfo,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_relInfo;
	}

	const char* sql_select_relMembers =
		"SELECT blob FROM tbl_relMembers WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_relMembers, -1,
	                      &self->stmt_select_relMembers,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_relMembers;
	}

	const char* sql_select_relRange =
		"SELECT blob FROM tbl_relRange WHERE id=@arg_id;";
	if(sqlite3_prepare_v2(self->db, sql_select_relRange, -1,
	                      &self->stmt_select_relRange,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_relRange;
	}

	self->idx_insert_nodeCoordId    = sqlite3_bind_parameter_index(self->stmt_insert_nodeCoord,  "@arg_id");
	self->idx_insert_nodeCoordBlob  = sqlite3_bind_parameter_index(self->stmt_insert_nodeCoord,  "@arg_blob");
	self->idx_insert_nodeInfoId     = sqlite3_bind_parameter_index(self->stmt_insert_nodeInfo,   "@arg_id");
	self->idx_insert_nodeInfoBlob   = sqlite3_bind_parameter_index(self->stmt_insert_nodeInfo,   "@arg_blob");
	self->idx_insert_wayInfoId      = sqlite3_bind_parameter_index(self->stmt_insert_wayInfo,    "@arg_id");
	self->idx_insert_wayInfoBlob    = sqlite3_bind_parameter_index(self->stmt_insert_wayInfo,    "@arg_blob");
	self->idx_insert_wayRangeId     = sqlite3_bind_parameter_index(self->stmt_insert_wayRange,   "@arg_id");
	self->idx_insert_wayRangeBlob   = sqlite3_bind_parameter_index(self->stmt_insert_wayRange,   "@arg_blob");
	self->idx_insert_wayNdsId       = sqlite3_bind_parameter_index(self->stmt_insert_wayNds,     "@arg_id");
	self->idx_insert_wayNdsBlob     = sqlite3_bind_parameter_index(self->stmt_insert_wayNds,     "@arg_blob");
	self->idx_insert_relInfoId      = sqlite3_bind_parameter_index(self->stmt_insert_relInfo,    "@arg_id");
	self->idx_insert_relInfoBlob    = sqlite3_bind_parameter_index(self->stmt_insert_relInfo,    "@arg_blob");
	self->idx_insert_relMembersId   = sqlite3_bind_parameter_index(self->stmt_insert_relMembers, "@arg_id");
	self->idx_insert_relMembersBlob = sqlite3_bind_parameter_index(self->stmt_insert_relMembers, "@arg_blob");
	self->idx_insert_relRangeId     = sqlite3_bind_parameter_index(self->stmt_insert_relRange,   "@arg_id");
	self->idx_insert_relRangeBlob   = sqlite3_bind_parameter_index(self->stmt_insert_relRange,   "@arg_blob");
	self->idx_select_nodeCoordId    = sqlite3_bind_parameter_index(self->stmt_select_nodeCoord,  "@arg_id");
	self->idx_select_nodeInfoId     = sqlite3_bind_parameter_index(self->stmt_select_nodeInfo,   "@arg_id");
	self->idx_select_wayInfoId      = sqlite3_bind_parameter_index(self->stmt_select_wayInfo,    "@arg_id");
	self->idx_select_wayRangeId     = sqlite3_bind_parameter_index(self->stmt_select_wayRange,   "@arg_id");
	self->idx_select_wayNdsId       = sqlite3_bind_parameter_index(self->stmt_select_wayNds,     "@arg_id");
	self->idx_select_relInfoId      = sqlite3_bind_parameter_index(self->stmt_select_relInfo,    "@arg_id");
	self->idx_select_relMembersId   = sqlite3_bind_parameter_index(self->stmt_select_relMembers, "@arg_id");
	self->idx_select_relRangeId     = sqlite3_bind_parameter_index(self->stmt_select_relRange,   "@arg_id");

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
		sqlite3_finalize(self->stmt_select_relRange);
	fail_prepare_select_relRange:
		sqlite3_finalize(self->stmt_select_relMembers);
	fail_prepare_select_relMembers:
		sqlite3_finalize(self->stmt_select_relInfo);
	fail_prepare_select_relInfo:
		sqlite3_finalize(self->stmt_select_wayNds);
	fail_prepare_select_wayNds:
		sqlite3_finalize(self->stmt_select_wayRange);
	fail_prepare_select_wayRange:
		sqlite3_finalize(self->stmt_select_wayInfo);
	fail_prepare_select_wayInfo:
		sqlite3_finalize(self->stmt_select_nodeInfo);
	fail_prepare_select_nodeInfo:
		sqlite3_finalize(self->stmt_select_nodeCoord);
	fail_prepare_select_nodeCoord:
		sqlite3_finalize(self->stmt_insert_relRange);
	fail_prepare_insert_relRange:
		sqlite3_finalize(self->stmt_insert_relMembers);
	fail_prepare_insert_relMembers:
		sqlite3_finalize(self->stmt_insert_relInfo);
	fail_prepare_insert_relInfo:
		sqlite3_finalize(self->stmt_insert_wayNds);
	fail_prepare_insert_wayNds:
		sqlite3_finalize(self->stmt_insert_wayRange);
	fail_prepare_insert_wayRange:
		sqlite3_finalize(self->stmt_insert_wayInfo);
	fail_prepare_insert_wayInfo:
		sqlite3_finalize(self->stmt_insert_nodeInfo);
	fail_prepare_insert_nodeInfo:
		sqlite3_finalize(self->stmt_insert_nodeCoord);
	fail_prepare_insert_nodeCoord:
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
		sqlite3_finalize(self->stmt_select_relRange);
		sqlite3_finalize(self->stmt_select_relMembers);
		sqlite3_finalize(self->stmt_select_relInfo);
		sqlite3_finalize(self->stmt_select_wayNds);
		sqlite3_finalize(self->stmt_select_wayRange);
		sqlite3_finalize(self->stmt_select_wayInfo);
		sqlite3_finalize(self->stmt_select_nodeInfo);
		sqlite3_finalize(self->stmt_select_nodeCoord);
		sqlite3_finalize(self->stmt_insert_relRange);
		sqlite3_finalize(self->stmt_insert_relMembers);
		sqlite3_finalize(self->stmt_insert_relInfo);
		sqlite3_finalize(self->stmt_insert_wayNds);
		sqlite3_finalize(self->stmt_insert_wayRange);
		sqlite3_finalize(self->stmt_insert_wayInfo);
		sqlite3_finalize(self->stmt_insert_nodeInfo);
		sqlite3_finalize(self->stmt_insert_nodeCoord);
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

int osmdb_index_get(osmdb_index_t* self,
                    int type, int64_t id,
                    osmdb_blob_t** _blob)
{
	ASSERT(self);
	ASSERT(_blob);

	int64_t major_id = id/OSMDB_BLOB_SIZE;
	int64_t minor_id = id%OSMDB_BLOB_SIZE;

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

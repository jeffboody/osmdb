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

#include <stdio.h>
#include <stdlib.h>

#define LOG_TAG "osmdb"
#include "libcc/math/cc_pow2n.h"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "osmdb/osmdb_util.h"
#include "osmdb_cache.h"

#define BATCH_SIZE 10000

typedef struct
{
	const char* fname;

	int nth;
	int mode;

	int64_t changeset;

	double latT;
	double lonL;
	double latB;
	double lonR;
} osmdb_cacheInfo_t;

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_cache_createTables(osmdb_cache_t* self,
                         osmdb_cacheInfo_t* info)
{
	ASSERT(self);
	ASSERT(info);

	const char* sql_init[] =
	{
		"PRAGMA temp_store_directory = '.';",
		"CREATE TABLE tbl_attr"
		"("
		"	key TEXT UNIQUE,"
		"	val TEXT"
		");",
		"CREATE TABLE tbl_tile"
		"("
		"	id   INTEGER PRIMARY KEY NOT NULL,"
		"	blob BLOB"
		");",
		NULL
	};

	// init sqlite3
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

	// insert changeset
	char sql[256];
	snprintf(sql, 256,
	         "INSERT INTO tbl_attr (key, val) VALUES ('changeset', '%" PRId64 "');",
	         info->changeset);
	if(sqlite3_exec(self->db, sql, NULL, NULL,
	                NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_exec: %s", sqlite3_errmsg(self->db));
		return 0;
	}

	// insert bounds
	snprintf(sql, 256,
	         "INSERT INTO tbl_attr (key, val)"
	         "	VALUES ('bounds', '%lf %lf %lf %lf');",
	         info->latT, info->lonL, info->latB, info->lonR);
	if(sqlite3_exec(self->db, sql, NULL, NULL,
	                NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_exec: %s", sqlite3_errmsg(self->db));
		return 0;
	}

	return 1;
}

static int
osmdb_cache_endTransaction(osmdb_cache_t* self)
{
	ASSERT(self);

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
		LOGE("sqlite3_step: %s", sqlite3_errmsg(self->db));
		goto fail_step;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	// success
	return 1;

	// failure
	fail_step:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	return 0;
}

static int
osmdb_cache_beginTransaction(osmdb_cache_t* self)
{
	ASSERT(self);

	if(self->batch_size >= BATCH_SIZE)
	{
		if(osmdb_cache_endTransaction(self) == 0)
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
		LOGE("sqlite3_step: %s", sqlite3_errmsg(self->db));
		goto fail_step;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	// success
	return 1;

	// failure
	fail_step:
	{
		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	return 0;
}

static osmdb_cache_t*
osmdb_cache_new(osmdb_cacheInfo_t* info)
{
	ASSERT(info);

	int exists = osmdb_fileExists(info->fname);
	if(((info->mode == OSMDB_CACHE_MODE_IMPORT) &&
	    (exists == 0)) ||
	   ((info->mode == OSMDB_CACHE_MODE_CREATE) &&
	    (exists == 1)))
	{
		LOGE("invalid %s", info->fname);
		return NULL;
	}

	osmdb_cache_t* self;
	self = (osmdb_cache_t*)
	       CALLOC(1, sizeof(osmdb_cache_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->mode = info->mode;
	self->nth  = info->nth;

	// sqlite3 is initialized by tiler for create
	int flags = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
	if(info->mode == OSMDB_CACHE_MODE_IMPORT)
	{
		if(sqlite3_initialize() != SQLITE_OK)
		{
			LOGE("sqlite3_initialize failed");
			goto fail_init;
		}
		flags = SQLITE_OPEN_READONLY;
	}

	if(sqlite3_open_v2(info->fname, &self->db, flags,
	                   NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 %s failed", info->fname);
		goto fail_db_open;
	}

	if(osmdb_cache_createTables(self, info) == 0)
	{
		goto fail_tables;
	}

	const char* sql_begin = "BEGIN;";
	if(sqlite3_prepare_v2(self->db, sql_begin, -1,
	                      &self->stmt_begin,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s",
		     sqlite3_errmsg(self->db));
		goto fail_prepare_begin;
	}

	const char* sql_end = "END;";
	if(sqlite3_prepare_v2(self->db, sql_end, -1,
	                      &self->stmt_end,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s",
		     sqlite3_errmsg(self->db));
		goto fail_prepare_end;
	}

	const char* sql_save = "INSERT INTO tbl_tile (id, blob)"
	                       "	VALUES (@arg_id, @arg_blob);";
	if(sqlite3_prepare_v2(self->db, sql_save, -1,
	                      &self->stmt_save,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s",
		     sqlite3_errmsg(self->db));
		goto fail_prepare_save;
	}

	self->stmt_load = (sqlite3_stmt**)
	                  CALLOC(info->nth, sizeof(sqlite3_stmt*));
	if(self->stmt_load == NULL)
	{
		goto fail_alloc_load;
	}

	int i;
	for(i = 0; i < info->nth; ++i)
	{
		const char* sql_load;
		sql_load = "SELECT blob FROM tbl_tile WHERE id=@arg_id;";
		if(sqlite3_prepare_v2(self->db, sql_load, -1,
		                      &self->stmt_load[i],
		                      NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_prepare_v2: %s",
			     sqlite3_errmsg(self->db));
			goto fail_prepare_load;
		}
	}

	self->idx_save_id   = sqlite3_bind_parameter_index(self->stmt_save,
	                                                   "@arg_id");
	self->idx_save_blob = sqlite3_bind_parameter_index(self->stmt_save,
	                                                   "@arg_blob");
	self->idx_load_id   = sqlite3_bind_parameter_index(self->stmt_load[0],
	                                                   "@arg_id");

	// success
	return self;

	// failure
	fail_prepare_load:
	{
		int k;
		for(k = 0; k < i; ++k)
		{
			sqlite3_finalize(self->stmt_load[k]);
		}
		FREE(self->stmt_load);
	}
	fail_alloc_load:
		sqlite3_finalize(self->stmt_save);
	fail_prepare_save:
		sqlite3_finalize(self->stmt_end);
	fail_prepare_end:
		sqlite3_finalize(self->stmt_begin);
	fail_prepare_begin:
	fail_tables:
	fail_db_open:
	{
		// close db even when open fails
		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		// sqlite3 is shutdown by tiler for create
		if(info->mode == OSMDB_CACHE_MODE_IMPORT)
		{
			if(sqlite3_shutdown() != SQLITE_OK)
			{
				LOGW("sqlite3_shutdown failed");
			}
		}
	}
	fail_init:
		FREE(self);
	return NULL;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_cache_t*
osmdb_cache_create(const char* fname, int64_t changeset,
                   double latT, double lonL,
                   double latB, double lonR)
{
	ASSERT(fname);

	osmdb_cacheInfo_t info =
	{
		.fname     = fname,
		.nth       = 1,
		.mode      = OSMDB_CACHE_MODE_CREATE,
		.changeset = changeset,
		.latT      = latT,
		.lonL      = lonL,
		.latB      = latB,
		.lonR      = lonR
	};

	return osmdb_cache_new(&info);
}

osmdb_cache_t*
osmdb_cache_import(const char* fname, int nth)
{
	ASSERT(fname);

	osmdb_cacheInfo_t info =
	{
		.fname = fname,
		.nth   = nth,
		.mode  = OSMDB_CACHE_MODE_IMPORT
	};

	return osmdb_cache_new(&info);
}

void osmdb_cache_delete(osmdb_cache_t** _self)
{
	ASSERT(_self);

	osmdb_cache_t* self = *_self;
	if(self)
	{
		if(osmdb_cache_endTransaction(self) == 0)
		{
			// ignore
		}

		int i;
		for(i = 0; i < self->nth; ++i)
		{
			sqlite3_finalize(self->stmt_load[i]);
		}
		FREE(self->stmt_load);

		sqlite3_finalize(self->stmt_save);
		sqlite3_finalize(self->stmt_end);
		sqlite3_finalize(self->stmt_begin);

		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(self->mode == OSMDB_CACHE_MODE_IMPORT)
		{
			if(sqlite3_shutdown() != SQLITE_OK)
			{
				LOGW("sqlite3_shutdown failed");
			}
		}

		FREE(self);
		*_self = NULL;
	}
}

int osmdb_cache_save(osmdb_cache_t* self,
                     int zoom, int x, int y,
                     int size, const void* data)
{
	ASSERT(self);

	if(osmdb_cache_beginTransaction(self) == 0)
	{
		return 0;
	}

	int64_t pow220   = cc_pow2n(20);
	int     idx_id   = self->idx_save_id;
	int     idx_blob = self->idx_save_blob;
	int64_t zoom64   = (int64_t) zoom;
	int64_t x64      = (int64_t) x;
	int64_t y64      = (int64_t) y;
	int64_t id       = zoom64 + 256*x64 + 256*pow220*y64;

	sqlite3_stmt* stmt = self->stmt_save;
	if((sqlite3_bind_int64(stmt, idx_id, id) != SQLITE_OK) ||
	   (sqlite3_bind_blob(stmt, idx_blob,
	                      data, size,
	                      SQLITE_TRANSIENT) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step: %s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

int osmdb_cache_load(osmdb_cache_t* self, int tid,
                     int zoom, int x, int y, void* priv,
                     osmdb_cacheLoaded_fn loaded_fn)
{
	ASSERT(self);
	ASSERT(loaded_fn);

	int64_t pow220 = cc_pow2n(20);
	int     idx_id = self->idx_load_id;
	int64_t zoom64 = (int64_t) zoom;
	int64_t x64    = (int64_t) x;
	int64_t y64    = (int64_t) y;
	int64_t id     = zoom64 + 256*x64 + 256*pow220*y64;

	sqlite3_stmt* stmt = self->stmt_load[tid];
	if(sqlite3_bind_int64(stmt, idx_id, id) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_int64 failed");
		return 0;
	}

	int         ret  = 1;
	int         size = 0;
	const void* data = NULL;
	if(sqlite3_step(stmt)== SQLITE_ROW)
	{
		size = sqlite3_column_bytes(stmt, 0);
		data = sqlite3_column_blob(stmt, 0);
		ret  = (*loaded_fn)(priv, size, data);
	}
	else
	{
		LOGE("sqlite3_step: msg=%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

int64_t osmdb_cache_changeset(osmdb_cache_t* self)
{
	ASSERT(self);

	int64_t       changeset = 0;
	const char*   sql;
	sqlite3_stmt* stmt;
	sql = "SELECT val FROM tbl_attr WHERE key='changeset';";
	if(sqlite3_prepare_v2(self->db, sql, -1,
	                      &stmt, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		return 0;
	}

	if(sqlite3_step(stmt) == SQLITE_DONE)
	{
		const unsigned char* val;
		val = sqlite3_column_text(stmt, 0);
		changeset = (int64_t)
		            strtoll((const char*) val, NULL, 0);
	}
	else
	{
		LOGE("sqlite3_step: %s", sqlite3_errmsg(self->db));
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	sqlite3_finalize(stmt);

	return changeset;
}

void osmdb_cache_bounds(osmdb_cache_t* self,
                        double* _latT,
                        double* _lonL,
                        double* _latB,
                        double* _lonR)
{
	ASSERT(self);
	ASSERT(_latT);
	ASSERT(_lonL);
	ASSERT(_latB);
	ASSERT(_lonR);

	*_latT = 0.0;
	*_lonL = 0.0;
	*_latB = 0.0;
	*_lonR = 0.0;

	const char*   sql;
	sqlite3_stmt* stmt;
	sql = "SELECT val FROM tbl_attr WHERE key='bounds';";
	if(sqlite3_prepare_v2(self->db, sql, -1,
	                      &stmt, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		return;
	}

	if(sqlite3_step(stmt) == SQLITE_DONE)
	{
		const char* val;
		val = (const char*) sqlite3_column_text(stmt, 0);
		sscanf(val, "%lf %lf %lf %lf",
		       _latT, _lonL, _latB, _lonR);
	}
	else
	{
		LOGE("sqlite3_step: %s", sqlite3_errmsg(self->db));
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	sqlite3_finalize(stmt);
}

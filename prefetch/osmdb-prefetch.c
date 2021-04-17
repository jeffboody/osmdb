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
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "osmdb-prefetch"
#include "libcc/math/cc_pow2n.h"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_timestamp.h"
#include "libsqlite3/sqlite3.h"
#include "osmdb/tiler/osmdb_tiler.h"
#include "osmdb/osmdb_util.h"
#include "terrain/terrain_util.h"

#define MODE_WW 0
#define MODE_US 1
#define MODE_CO 2

#define BATCH_SIZE 10000

#define NZOOM 3
const int ZOOM_LEVEL[] =
{
	9, 12, 15
};

// sampling rectangles
const double WW_LATT = 90;
const double WW_LONL = -180.0;
const double WW_LATB = -90.0;
const double WW_LONR = 180.0;
const double US_LATT = 51.0;
const double US_LONL = -126.0;
const double US_LATB = 23.0;
const double US_LONR = -64.0;
const double CO_LATT = 43.0;
const double CO_LONL = -110.0;
const double CO_LATB = 34.0;
const double CO_LONR = -100.0;

typedef struct
{
	int      mode;
	double   t0;
	double   latT;
	double   lonL;
	double   latB;
	double   lonR;
	uint64_t count;
	uint64_t total;

	osmdb_tiler_t* tiler;

	sqlite3* db;

	// sqlite3 statements
	int           batch_size;
	sqlite3_stmt* stmt_begin;
	sqlite3_stmt* stmt_end;
	sqlite3_stmt* stmt_save[NZOOM];

	// sqlite3 indices
	int idx_save_id;
	int idx_save_blob;
} osmdb_prefetch_t;

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_prefetch_createTables(osmdb_prefetch_t* self)
{
	ASSERT(self);

	const char* sql_init[] =
	{
		"PRAGMA temp_store_directory = '.';",
		"CREATE TABLE tbl_attr"
		"("
		"	key TEXT UNIQUE,"
		"	val TEXT"
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

	// create tbl_tile[i]
	for(i = 0; i < NZOOM; ++i)
	{
		char sql_tbl[256];
		snprintf(sql_tbl, 256,
		         "CREATE TABLE tbl_tile%i"
		         "("
		         "	id   INTEGER PRIMARY KEY NOT NULL,"
		         "	blob BLOB"
		         ");", ZOOM_LEVEL[i]);

		if(sqlite3_exec(self->db, sql_tbl, NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec(%i): %s",
			     i, sqlite3_errmsg(self->db));
			return 0;
		}
	}

	// insert changeset
	int64_t changeset = self->tiler->changeset;
	char    sql_changeset[256];
	snprintf(sql_changeset, 256,
	         "INSERT INTO tbl_attr (key, val)"
	         "	VALUES ('changeset', '%" PRId64 "');",
	         changeset);

	if(sqlite3_exec(self->db, sql_changeset, NULL, NULL,
	                NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_exec(%i): %s",
		     i, sqlite3_errmsg(self->db));
		return 0;
	}

	return 1;
}

static int
osmdb_prefetch_endTransaction(osmdb_prefetch_t* self)
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
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
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
osmdb_prefetch_beginTransaction(osmdb_prefetch_t* self)
{
	ASSERT(self);

	if(self->batch_size >= BATCH_SIZE)
	{
		if(osmdb_prefetch_endTransaction(self) == 0)
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
osmdb_prefetch_make(osmdb_prefetch_t* self,
                    int zoom, int x, int y)
{
	ASSERT(self);

	int izoom;
	for(izoom = 0; izoom < NZOOM; ++izoom)
	{
		if(zoom == ZOOM_LEVEL[izoom])
		{
			break;
		}
	}

	if(izoom == NZOOM)
	{
		LOGE("invalid izoom=%i", izoom);
		return 0;
	}

	size_t size = 0;
	void*  data;
	data = (void*)
	       osmdb_tiler_make(self->tiler, 0,
	                        zoom, x, y, &size);
	if((data == NULL) || (size > INT_MAX))
	{
		FREE(data);
		return 0;
	}

	int     idx_id   = self->idx_save_id;
	int     idx_blob = self->idx_save_blob;
	int64_t id       = cc_pow2n(zoom)*y + x;

	sqlite3_stmt* stmt = self->stmt_save[izoom];
	if((sqlite3_bind_int64(stmt, idx_id, id) != SQLITE_OK) ||
	   (sqlite3_bind_blob(stmt, idx_blob, data, (int) size,
	                      SQLITE_TRANSIENT) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		goto fail_bind;
	}

	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step: %s",
		     sqlite3_errmsg(self->db));
		goto fail_step;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	FREE(data);

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
	fail_bind:
		FREE(data);
	return 0;
}

static int
osmdb_prefetch_tile(osmdb_prefetch_t* self,
                    int zoom, int x, int y)
{
	ASSERT(self);

	if(osmdb_prefetch_beginTransaction(self) == 0)
	{
		return 0;
	}

	if(osmdb_prefetch_make(self, zoom, x, y) == 0)
	{
		return 0;
	}

	// update prefetch state
	if((self->count % 1000) == 0)
	{
		double dt;
		double progress;
		dt       = cc_timestamp() - self->t0;
		progress = 100.0*(((double) self->count)/
		                  ((double) self->total));
		printf("[PF] dt=%0.2lf, %" PRIu64 "/%" PRIu64 ", progress=%lf\n",
		       dt, self->count, self->total, progress);
	}
	++self->count;

	return 1;
}

static int
osmdb_prefetch_tiles(osmdb_prefetch_t* self,
                     int zoom, int x, int y)
{
	ASSERT(self);

	// clip tile
	if(self->mode != MODE_WW)
	{
		// compute tile bounds
		double latT = WW_LATT;
		double lonL = WW_LONL;
		double latB = WW_LATB;
		double lonR = WW_LONR;
		terrain_bounds(x, y, zoom, &latT, &lonL, &latB, &lonR);

		if((latT < self->latB) || (lonL > self->lonR) ||
		   (latB > self->latT) || (lonR < self->lonL))
		{
			return 1;
		}
	}

	// prefetch tile
	if((zoom == 9) || (zoom == 12) || (zoom == 15))
	{
		if(osmdb_prefetch_tile(self, zoom, x, y) == 0)
		{
			return 0;
		}
	}

	// prefetch subtiles
	if(zoom < 15)
	{
		int zoom2 = zoom + 1;
		int x2    = 2*x;
		int y2    = 2*y;
		return osmdb_prefetch_tiles(self, zoom2, x2,     y2)     &&
		       osmdb_prefetch_tiles(self, zoom2, x2 + 1, y2)     &&
		       osmdb_prefetch_tiles(self, zoom2, x2,     y2 + 1) &&
		       osmdb_prefetch_tiles(self, zoom2, x2 + 1, y2 + 1);
	}

	return 1;
}

static uint64_t
osmdb_prefetch_range(osmdb_prefetch_t* self, int zoom)
{
	float x0f;
	float y0f;
	float x1f;
	float y1f;
	terrain_coord2tile(self->latT, self->lonL,
	                   zoom, &x0f, &y0f);
	terrain_coord2tile(self->latB, self->lonR,
	                   zoom, &x1f, &y1f);

	uint64_t x0 = (uint64_t) x0f;
	uint64_t y0 = (uint64_t) y0f;
	uint64_t x1 = (uint64_t) x1f;
	uint64_t y1 = (uint64_t) y1f;

	return (x1 - x0)*(y1 - y0);
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	int         ret         = EXIT_FAILURE;
	int         usage       = 1;
	int         mode        = MODE_WW;
	float       smem        = 1.0f;
	const char* fname_cache = NULL;
	const char* fname_index = NULL;
	double      latT        = WW_LATT;
	double      lonL        = WW_LONL;
	double      latB        = WW_LATB;
	double      lonR        = WW_LONR;
	if(argc == 5)
	{
		if(strcmp(argv[1], "-pf=CO") == 0)
		{
			mode  = MODE_CO;
			latT  = CO_LATT;
			lonL  = CO_LONL;
			latB  = CO_LATB;
			lonR  = CO_LONR;
			usage = 0;
		}
		else if(strcmp(argv[1], "-pf=US") == 0)
		{
			mode  = MODE_US;
			latT  = CO_LATT;
			lonL  = CO_LONL;
			latB  = CO_LATB;
			lonR  = CO_LONR;
			usage = 0;
		}
		else if(strcmp(argv[1], "-pf=WW") == 0)
		{
			mode  = MODE_WW;
			usage = 0;
		}

		smem        = strtof(argv[2], NULL);
		fname_cache = argv[3];
		fname_index = argv[4];
	}

	if(usage)
	{
		LOGE("usage: %s [PREFETCH] [SMEM] cache.sqlite3 index.sqlite3",
		     argv[0]);
		LOGE("PREFETCH:");
		LOGE("-pf=CO (Colorado)");
		LOGE("-pf=US (United States)");
		LOGE("-pf=WW (Worldwide)");
		LOGE("SMEM: scale memory in GB (e.g. 1.0)");
		return EXIT_FAILURE;
	}

	if(osmdb_fileExists(fname_cache))
	{
		LOGE("invalid %s", fname_cache);
		return EXIT_FAILURE;
	}

	osmdb_prefetch_t* self;
	self = (osmdb_prefetch_t*)
	       CALLOC(1, sizeof(osmdb_prefetch_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return EXIT_FAILURE;
	}

	self->mode  = mode;
	self->t0    = cc_timestamp();

	self->latT = latT;
	self->lonL = lonL;
	self->latB = latB;
	self->lonR = lonR;

	// estimate the total
	self->count = 0;
	self->total = 0;
	self->total += osmdb_prefetch_range(self, 9);
	self->total += osmdb_prefetch_range(self, 12);
	self->total += osmdb_prefetch_range(self, 15);

	self->tiler = osmdb_tiler_new(fname_index, 1, smem);
	if(self->tiler == NULL)
	{
		goto fail_tiler;
	}

	int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
	if(sqlite3_open_v2(fname_cache, &self->db,
	                   flags, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 %s failed", fname_cache);
		goto fail_db_open;
	}

	if(osmdb_prefetch_createTables(self) == 0)
	{
		goto fail_createTables;
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

	int  i;
	char sql_save[256];
	for(i = 0; i < NZOOM; ++i)
	{
		snprintf(sql_save, 256,
		         "INSERT INTO tbl_tile%i (id, blob)"
		         "	VALUES (@arg_id, @arg_blob);",
		         ZOOM_LEVEL[i]);
		if(sqlite3_prepare_v2(self->db, sql_save, -1,
		                      &self->stmt_save[i],
		                      NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_prepare_v2: %s",
			     sqlite3_errmsg(self->db));
			goto fail_prepare_save;
		}
	}

	self->idx_save_id   = sqlite3_bind_parameter_index(self->stmt_save[0],
	                                                   "@arg_id");
	self->idx_save_blob = sqlite3_bind_parameter_index(self->stmt_save[0],
	                                                   "@arg_blob");

	if(osmdb_prefetch_tiles(self, 0, 0, 0) == 0)
	{
		goto fail_run;
	}

	osmdb_prefetch_endTransaction(self);

	// success
	// fall through
	ret = EXIT_SUCCESS;

	// failure
	fail_run:
	{
		osmdb_prefetch_endTransaction(self);

		int k;
		for(k = 0; k < i; ++k)
		{
			sqlite3_finalize(self->stmt_save[k]);
		}
	}
	fail_prepare_save:
		sqlite3_finalize(self->stmt_end);
	fail_prepare_end:
		sqlite3_finalize(self->stmt_begin);
	fail_prepare_begin:
	fail_createTables:
	fail_db_open:
	{
		// close db even when open fails
		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		// tiler shuts down sqlite
		osmdb_tiler_delete(&self->tiler);
	}
	fail_tiler:
	{
		FREE(self);

		if(ret == EXIT_SUCCESS)
		{
			LOGI("SUCCESS");
		}
		else
		{
			LOGE("FAILURE");
		}
	}
	return ret;
}

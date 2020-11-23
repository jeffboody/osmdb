/*
 * Copyright (c) 2020 Jeff Boody
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libxmlstream/xml_istream.h"
#include "osm_parser.h"
#include "../osmdb_util.h"

#define OSM_STATE_INIT          0
#define OSM_STATE_OSM           1
#define OSM_STATE_OSM_BOUNDS    2
#define OSM_STATE_OSM_CHANGESET 3
#define OSM_STATE_DONE          -1

/***********************************************************
* private                                                  *
***********************************************************/

static void osm_parser_init(osm_parser_t* self)
{
	ASSERT(self);

	self->attr_change_id = 0.0;
	self->attr_min_lat   = 0.0;
	self->attr_min_lon   = 0.0;
	self->attr_max_lat   = 0.0;
	self->attr_max_lon   = 0.0;
}

static int osm_parser_createWaysRange(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_create_ways_range;

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int osm_parser_createRelsRange(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_create_rels_range;

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static void osm_parser_dropWaysRange(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_drop_rels_range;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}
}

static void osm_parser_dropRelsRange(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_drop_rels_range;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}
}

static int
osm_parser_insertWaysRange(osm_parser_t* self,
                           double latT, double lonL,
                           double latB, double lonR)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_insert_ways_range;

	int idx_latT = self->idx_insert_ways_range_latT;
	int idx_lonL = self->idx_insert_ways_range_lonL;
	int idx_latB = self->idx_insert_ways_range_latB;
	int idx_lonR = self->idx_insert_ways_range_lonR;

	if((sqlite3_bind_double(stmt, idx_latT, latT) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonL, lonL) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_latB, latB) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonR, lonR) != SQLITE_OK))
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertRelsRange(osm_parser_t* self,
                           double latT, double lonL,
                           double latB, double lonR)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_insert_rels_range;

	int idx_latT = self->idx_insert_rels_range_latT;
	int idx_lonL = self->idx_insert_rels_range_lonL;
	int idx_latB = self->idx_insert_rels_range_latB;
	int idx_lonR = self->idx_insert_rels_range_lonR;

	if((sqlite3_bind_double(stmt, idx_latT, latT) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonL, lonL) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_latB, latB) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lonR, lonR) != SQLITE_OK))
	{
		LOGE("sqlite3_bind_double failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int osm_parser_deleteWaysRange(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_delete_ways_range;

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int osm_parser_deleteRelsRange(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_stmt* stmt = self->stmt_delete_rels_range;

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("%s", sqlite3_errmsg(self->db));
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_beginOsm(osm_parser_t* self, int line,
                    const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM;

	return 1;
}

static int
osm_parser_endOsm(osm_parser_t* self, int line,
                  const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_DONE;

	return 1;
}

static int
osm_parser_beginOsmBounds(osm_parser_t* self, int line,
                          const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_BOUNDS;

	return 1;
}

static int
osm_parser_endOsmBounds(osm_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	return 1;
}

static int
osm_parser_beginOsmChangeset(osm_parser_t* self, int line,
                             const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_CHANGESET;
	osm_parser_init(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->attr_change_id = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "min_lat") == 0)
		{
			self->attr_min_lat = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "min_lon") == 0)
		{
			self->attr_min_lon = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "max_lat") == 0)
		{
			self->attr_max_lat = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "max_lon") == 0)
		{
			self->attr_max_lon = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_endOsmChangeset(osm_parser_t* self, int line,
                           const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	if(fmod(self->attr_change_id, 100000.0) == 0.0)
	{
		LOGI("id=%0.0lf", self->attr_change_id);
	}

	// check for valid changes
	if((self->attr_change_id <= self->change_id) ||
	   ((self->attr_min_lat == 0.0) &&
	    (self->attr_min_lon == 0.0) &&
	    (self->attr_max_lat == 0.0) &&
	    (self->attr_max_lon == 0.0)))
	{
		// ignore
		return 1;
	}

	double latT = self->attr_max_lat;
	double lonL = self->attr_min_lon;
	double latB = self->attr_min_lat;
	double lonR = self->attr_max_lon;
	if((osm_parser_insertWaysRange(self, latT, lonL,
	                               latB, lonR) == 0) ||
	   (osm_parser_insertRelsRange(self, latT, lonL,
	                               latB, lonR) == 0))
	{
		return 0;
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osm_parser_t*
osm_parser_new(double change_id, const char* fname)
{
	ASSERT(fname);

	osm_parser_t* self = (osm_parser_t*)
	                     CALLOC(1, sizeof(osm_parser_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->change_id = change_id;

	if(sqlite3_initialize() != SQLITE_OK)
	{
		LOGE("sqlite3_initialize failed");
		goto fail_init;
	}

	if(sqlite3_open_v2(fname, &self->db,
	                   SQLITE_OPEN_READWRITE,
	                   NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 failed");
		goto fail_open;
	}

	sqlite3_enable_load_extension(self->db, 1);
	if(sqlite3_load_extension(self->db, "./spellfix.so",
	                          NULL, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_load_extension failed");
		goto fail_extension;
	}

	const char* sql_create_stmt_ways_range =
		"CREATE TEMP TABLE tbl_delete_ways_range"
		"("
		"	wid INTEGER PRIMARY KEY"
		");";

	if(sqlite3_prepare_v2(self->db, sql_create_stmt_ways_range, -1,
	                      &self->stmt_create_ways_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_create_ways_range;
	}

	if(osm_parser_createWaysRange(self) == 0)
	{
		goto fail_create_ways;
	}

	const char* sql_create_stmt_rels_range =
		"CREATE TEMP TABLE tbl_delete_rels_range"
		"("
		"	rid INTEGER PRIMARY KEY"
		");";

	if(sqlite3_prepare_v2(self->db, sql_create_stmt_rels_range, -1,
	                      &self->stmt_create_rels_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_create_rels_range;
	}

	if(osm_parser_createRelsRange(self) == 0)
	{
		goto fail_create_rels;
	}

	const char* sql_drop_stmt_ways_range =
		"DROP TABLE tbl_delete_ways_range;";

	if(sqlite3_prepare_v2(self->db, sql_drop_stmt_ways_range, -1,
	                      &self->stmt_drop_ways_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_drop_ways_range;
	}

	const char* sql_drop_stmt_rels_range =
		"DROP TABLE tbl_delete_rels_range;";

	if(sqlite3_prepare_v2(self->db, sql_drop_stmt_rels_range, -1,
	                      &self->stmt_drop_rels_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_drop_rels_range;
	}

	const char* sql_insert_stmt_ways_range =
		"INSERT OR IGNORE INTO tbl_delete_ways_range (wid)"
		"	SELECT wid FROM tbl_ways_range"
		"	WHERE latT>@arg_latB AND lonL<@arg_lonR AND"
		"	      latB<@arg_latT AND lonR>@arg_lonL;";

	if(sqlite3_prepare_v2(self->db, sql_insert_stmt_ways_range, -1,
	                      &self->stmt_insert_ways_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_insert_ways_range;
	}

	const char* sql_insert_stmt_rels_range =
		"INSERT OR IGNORE INTO tbl_delete_rels_range (rid)"
		"	SELECT rid FROM tbl_rels_range"
		"	WHERE latT>@arg_latB AND lonL<@arg_lonR AND"
		"	      latB<@arg_latT AND lonR>@arg_lonL;";

	if(sqlite3_prepare_v2(self->db, sql_insert_stmt_rels_range, -1,
	                      &self->stmt_insert_rels_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_insert_rels_range;
	}

	const char* sql_delete_stmt_ways_range =
		"DELETE FROM tbl_ways_range WHERE EXISTS"
		"	( SELECT wid FROM tbl_delete_ways_range );";

	if(sqlite3_prepare_v2(self->db, sql_delete_stmt_ways_range, -1,
	                      &self->stmt_delete_ways_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_delete_ways_range;
	}

	const char* sql_delete_stmt_rels_range =
		"DELETE FROM tbl_rels_range WHERE EXISTS"
		"	( SELECT rid FROM tbl_delete_rels_range );";

	if(sqlite3_prepare_v2(self->db, sql_delete_stmt_rels_range, -1,
	                      &self->stmt_delete_rels_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_delete_rels_range;
	}

	self->idx_insert_ways_range_latT = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_latT");
	self->idx_insert_ways_range_lonL = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_lonL");
	self->idx_insert_ways_range_latB = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_latB");
	self->idx_insert_ways_range_lonR = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_lonR");
	self->idx_insert_rels_range_latT = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_latT");
	self->idx_insert_rels_range_lonL = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_lonL");
	self->idx_insert_rels_range_latB = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_latB");
	self->idx_insert_rels_range_lonR = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_lonR");

	// success
	return self;

	// failure
	fail_prepare_delete_rels_range:
		sqlite3_finalize(self->stmt_delete_ways_range);
	fail_prepare_delete_ways_range:
		sqlite3_finalize(self->stmt_insert_rels_range);
	fail_prepare_insert_rels_range:
		sqlite3_finalize(self->stmt_insert_ways_range);
	fail_prepare_insert_ways_range:
		sqlite3_finalize(self->stmt_drop_rels_range);
	fail_prepare_drop_rels_range:
		sqlite3_finalize(self->stmt_drop_ways_range);
	fail_prepare_drop_ways_range:
		osm_parser_dropRelsRange(self);
	fail_create_rels:
		sqlite3_finalize(self->stmt_create_rels_range);
	fail_prepare_create_rels_range:
		osm_parser_dropWaysRange(self);
	fail_create_ways:
		sqlite3_finalize(self->stmt_create_ways_range);
	fail_prepare_create_ways_range:
	fail_extension:
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

void osm_parser_delete(osm_parser_t** _self)
{
	ASSERT(_self);

	osm_parser_t* self = *_self;
	if(self)
	{
		sqlite3_finalize(self->stmt_delete_rels_range);
		sqlite3_finalize(self->stmt_delete_ways_range);
		sqlite3_finalize(self->stmt_insert_rels_range);
		sqlite3_finalize(self->stmt_insert_ways_range);
		sqlite3_finalize(self->stmt_drop_rels_range);
		sqlite3_finalize(self->stmt_drop_ways_range);
		osm_parser_dropRelsRange(self);
		sqlite3_finalize(self->stmt_create_rels_range);
		osm_parser_dropWaysRange(self);
		sqlite3_finalize(self->stmt_create_ways_range);


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

int osm_parser_start(void* priv, int line,
                     const char* name, const char** atts)
{
	ASSERT(priv);
	ASSERT(name);
	ASSERT(atts);

	osm_parser_t* self = (osm_parser_t*) priv;

	int state = self->state;
	if(state == OSM_STATE_INIT)
	{
		if(strcmp(name, "osm") == 0)
		{
			return osm_parser_beginOsm(self, line, atts);
		}
	}
	else if(state == OSM_STATE_OSM)
	{
		if(strcmp(name, "bound") == 0)
		{
			return osm_parser_beginOsmBounds(self, line, atts);
		}
		else if(strcmp(name, "changeset") == 0)
		{
			return osm_parser_beginOsmChangeset(self, line, atts);
		}
	}
	else if(state == OSM_STATE_OSM_CHANGESET)
	{
		++self->depth;
		return 1;
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

int osm_parser_end(void* priv, int line,
                   const char* name, const char* content)
{
	// content may be NULL
	ASSERT(priv);
	ASSERT(name);

	osm_parser_t* self = (osm_parser_t*) priv;


	int state = self->state;
	if(state == OSM_STATE_OSM)
	{
		return osm_parser_endOsm(self, line, content);
	}
	else if(state == OSM_STATE_OSM_BOUNDS)
	{
		return osm_parser_endOsmBounds(self, line, content);
	}
	else if(state == OSM_STATE_OSM_CHANGESET)
	{
		if(self->depth == 0)
		{
			return osm_parser_endOsmChangeset(self, line, content);
		}
		else
		{
			--self->depth;
			return 1;
		}
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

int osm_parser_finish(osm_parser_t* self)
{
	ASSERT(self);

	return osm_parser_deleteWaysRange(self) &&
	       osm_parser_deleteRelsRange(self);
}

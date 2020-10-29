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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "osmdb_database.h"
#include "osmdb_util.h"

/***********************************************************
* private                                                  *
***********************************************************/

static void
osmdb_database_cat(char* words, int* len, const char* word)
{
	ASSERT(words);
	ASSERT(word);

	int idx = 0;
	while(*len < 255)
	{
		if(word[idx] == '\0')
		{
			return;
		}

		words[*len] = word[idx];
		*len += 1;
		words[*len] = '\0';
		++idx;
	}
}

static int
osmdb_database_spellfixWord(osmdb_database_t* self,
                            const char* word,
                            char* spellfix, int* len)
{
	ASSERT(self);
	ASSERT(word);
	ASSERT(spellfix);

	int bytes = strlen(word) + 1;
	if(sqlite3_bind_text(self->stmt_spellfix,
	                     self->idx_spellfix_arg, word,
	                     bytes, SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	if(sqlite3_step(self->stmt_spellfix) == SQLITE_ROW)
	{
		const unsigned char* v;
		v = sqlite3_column_text(self->stmt_spellfix, 0);
		osmdb_database_cat(spellfix, len, (const char*) v);
	}
	else
	{
		osmdb_database_cat(spellfix, len, word);
	}

	if(sqlite3_reset(self->stmt_spellfix) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_searchTblNodes(osmdb_database_t* self,
                              const char* text,
                              xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	int           idx  = self->idx_search_nodes_arg;
	sqlite3_stmt* stmt = self->stmt_search_nodes;

	int bytes = strlen(text) + 1;
	if(sqlite3_bind_text(stmt, idx, text, bytes,
	                     SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		int         class = sqlite3_column_int(stmt, 0);
		const char* name  = (const char*)
		                    sqlite3_column_text(stmt, 1);
		const char* abrev = (const char*)
		                    sqlite3_column_text(stmt, 2);
		int         ele   = sqlite3_column_int(stmt, 3);
		int         st    = sqlite3_column_int(stmt, 4);
		double      lat   = sqlite3_column_double(stmt, 5);
		double      lon   = sqlite3_column_double(stmt, 6);

		xml_ostream_begin(os, "node");
		xml_ostream_attr(os, "name",
		                 abrev[0] == '\0' ? name : abrev);
		if(st)
		{
			xml_ostream_attr(os, "state", osmdb_stCodeToAbrev(st));
		}
		xml_ostream_attr(os, "class",
		                 osmdb_classCodeToName(class));
		xml_ostream_attrf(os, "rank", "%i",
		                 osmdb_classCodeToRank(class));
		xml_ostream_attrf(os, "lat", "%lf", lat);
		xml_ostream_attrf(os, "lon", "%lf", lon);
		if(ele)
		{
			xml_ostream_attrf(os, "ele", "%i",  ele);
		}
		xml_ostream_end(os);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_searchTblWays(osmdb_database_t* self,
                             const char* text,
                             xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	int           idx  = self->idx_search_ways_arg;
	sqlite3_stmt* stmt = self->stmt_search_ways;

	int bytes = strlen(text) + 1;
	if(sqlite3_bind_text(stmt, idx, text, bytes,
	                     SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		int         class = sqlite3_column_int(stmt, 0);
		const char* name  = (const char*)
		                    sqlite3_column_text(stmt, 1);
		const char* abrev = (const char*)
		                    sqlite3_column_text(stmt, 2);
		double      latT  = sqlite3_column_double(stmt, 3);
		double      lonL  = sqlite3_column_double(stmt, 4);
		double      latB  = sqlite3_column_double(stmt, 5);
		double      lonR  = sqlite3_column_double(stmt, 6);
		double      lat   = latB + (latT - latB)/2.0;
		double      lon   = lonL + (lonR - lonL)/2.0;

		xml_ostream_begin(os, "node");
		xml_ostream_attr(os, "name",
		                 abrev[0] == '\0' ? name : abrev);

		xml_ostream_attr(os, "class",
		                 osmdb_classCodeToName(class));
		xml_ostream_attrf(os, "rank", "%i",
		                 osmdb_classCodeToRank(class));
		xml_ostream_attrf(os, "lat", "%lf", lat);
		xml_ostream_attrf(os, "lon", "%lf", lon);
		xml_ostream_end(os);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

static int
osmdb_database_searchTblRels(osmdb_database_t* self,
                             const char* text,
                             xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	int           idx  = self->idx_search_rels_arg;
	sqlite3_stmt* stmt = self->stmt_search_rels;

	int bytes = strlen(text) + 1;
	if(sqlite3_bind_text(stmt, idx, text, bytes,
	                     SQLITE_STATIC) != SQLITE_OK)
	{
		LOGE("sqlite3_bind_text failed");
		return 0;
	}

	while(sqlite3_step(stmt) == SQLITE_ROW)
	{
		int         class = sqlite3_column_int(stmt, 0);
		const char* name  = (const char*)
		                    sqlite3_column_text(stmt, 1);
		const char* abrev = (const char*)
		                    sqlite3_column_text(stmt, 2);
		double      latT  = sqlite3_column_double(stmt, 3);
		double      lonL  = sqlite3_column_double(stmt, 4);
		double      latB  = sqlite3_column_double(stmt, 5);
		double      lonR  = sqlite3_column_double(stmt, 6);
		double      lat   = latB + (latT - latB)/2.0;
		double      lon   = lonL + (lonR - lonL)/2.0;

		xml_ostream_begin(os, "node");
		xml_ostream_attr(os, "name",
		                 abrev[0] == '\0' ? name : abrev);

		xml_ostream_attr(os, "class",
		                 osmdb_classCodeToName(class));
		xml_ostream_attrf(os, "rank", "%i",
		                 osmdb_classCodeToRank(class));
		xml_ostream_attrf(os, "lat", "%lf", lat);
		xml_ostream_attrf(os, "lon", "%lf", lon);
		xml_ostream_end(os);
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_database_t* osmdb_database_new(const char* fname)
{
	ASSERT(fname);

	osmdb_database_t* self;
	self = (osmdb_database_t*)
	       CALLOC(1, sizeof(osmdb_database_t));
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

	if(sqlite3_open_v2(fname, &self->db,
	                   SQLITE_OPEN_READONLY,
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

	const char sql_spellfix[256] =
		"SELECT word FROM tbl_spellfix"
		"	WHERE word MATCH @arg AND top=5;";

	if(sqlite3_prepare_v2(self->db, sql_spellfix, -1,
	                      &self->stmt_spellfix,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_spellfix;
	}

	const char sql_search_nodes[256] =
		"SELECT class, name, abrev, ele, st, lat, lon FROM tbl_nodes_text"
		"	JOIN tbl_nodes_info USING (nid)"
		"	JOIN tbl_nodes_coords USING (nid)"
		"	JOIN tbl_class_rank USING (class)"
		"	WHERE txt MATCH @arg"
		"	ORDER BY rank DESC"
		"	LIMIT 10;";

	if(sqlite3_prepare_v2(self->db, sql_search_nodes, -1,
	                      &self->stmt_search_nodes,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_search_nodes;
	}

	const char sql_search_ways[256] =
		"SELECT class, name, abrev, latT, lonL, latB, lonR FROM tbl_ways_text"
		"	JOIN tbl_ways USING (wid)"
		"	JOIN tbl_ways_range USING (wid)"
		"	JOIN tbl_class_rank USING (class)"
		"	WHERE txt MATCH @arg"
		"	ORDER BY rank DESC"
		"	LIMIT 10;";

	if(sqlite3_prepare_v2(self->db, sql_search_ways, -1,
	                      &self->stmt_search_ways,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_search_ways;
	}

	const char sql_search_rels[256] =
		"SELECT class, name, abrev, latT, lonL, latB, lonR FROM tbl_rels_text"
		"	JOIN tbl_rels USING (rid)"
		"	JOIN tbl_rels_range USING (rid)"
		"	JOIN tbl_class_rank USING (class)"
		"	WHERE txt MATCH @arg"
		"	ORDER BY rank DESC"
		"	LIMIT 10;";

	if(sqlite3_prepare_v2(self->db, sql_search_rels, -1,
	                      &self->stmt_search_rels,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2 failed");
		goto fail_prepare_search_rels;
	}

	self->idx_spellfix_arg     = sqlite3_bind_parameter_index(self->stmt_spellfix, "@arg");
	self->idx_search_nodes_arg = sqlite3_bind_parameter_index(self->stmt_search_nodes, "@arg");
	self->idx_search_ways_arg  = sqlite3_bind_parameter_index(self->stmt_search_ways, "@arg");
	self->idx_search_rels_arg  = sqlite3_bind_parameter_index(self->stmt_search_rels, "@arg");

	// success
	return self;

	// failure
	fail_prepare_search_rels:
		sqlite3_finalize(self->stmt_search_ways);
	fail_prepare_search_ways:
		sqlite3_finalize(self->stmt_search_nodes);
	fail_prepare_search_nodes:
		sqlite3_finalize(self->stmt_spellfix);
	fail_prepare_spellfix:
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

void osmdb_database_delete(osmdb_database_t** _self)
{
	ASSERT(_self);

	osmdb_database_t* self = *_self;
	if(self)
	{
		sqlite3_finalize(self->stmt_search_rels);
		sqlite3_finalize(self->stmt_search_ways);
		sqlite3_finalize(self->stmt_search_nodes);
		sqlite3_finalize(self->stmt_spellfix);

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

void osmdb_database_spellfix(osmdb_database_t* self,
                             const char* text,
                             char* spellfix)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(spellfix);

	// initialize spellfix
	spellfix[0] = '\0';

	char words[256];
	snprintf(words, 256, "%s", text);

	int idx = 0;
	int len = 0;
	const char* word = words;
	while(1)
	{
		if(words[idx] == ' ')
		{
			words[idx] = '\0';
			osmdb_database_spellfixWord(self, word, spellfix, &len);
			osmdb_database_cat(spellfix, &len, " ");
			word = &(words[idx + 1]);
		}
		else if(words[idx] == '\0')
		{
			osmdb_database_spellfixWord(self, word, spellfix, &len);
			return;
		}

		++idx;
	}
}

int osmdb_database_search(osmdb_database_t* self,
                          const char* text,
                          xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(text);
	ASSERT(os);

	xml_ostream_begin(os, "db");

	osmdb_database_searchTblNodes(self, text, os);
	osmdb_database_searchTblWays(self, text, os);
	osmdb_database_searchTblRels(self, text, os);

	xml_ostream_end(os);

	return 1;
}

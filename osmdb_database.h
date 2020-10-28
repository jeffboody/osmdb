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

#ifndef osmdb_database_H
#define osmdb_database_H

#include "libsqlite3/sqlite3.h"
#include "libxmlstream/xml_ostream.h"

typedef struct
{
	sqlite3* db;

	// search statements
	sqlite3_stmt* stmt_spellfix;
	sqlite3_stmt* stmt_search_nodes;
	sqlite3_stmt* stmt_search_ways;
	sqlite3_stmt* stmt_search_rels;

	// search arguments
	int idx_spellfix_arg;
	int idx_search_nodes_arg;
	int idx_search_ways_arg;
	int idx_search_rels_arg;
} osmdb_database_t;

osmdb_database_t* osmdb_database_new(const char* fname);
void              osmdb_database_delete(osmdb_database_t** _self);
void              osmdb_database_spellfix(osmdb_database_t* self,
                                          const char* text,
                                          char* spellfix);
int               osmdb_database_search(osmdb_database_t* self,
                                        const char* text,
                                        xml_ostream_t* os);

#endif

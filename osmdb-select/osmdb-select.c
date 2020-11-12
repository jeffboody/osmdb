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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libsqlite3/sqlite3.h"
#include "terrain/terrain_util.h"
#include "../osmdb_database.h"

/***********************************************************
* private                                                  *
***********************************************************/

#define OSMDB_REQUEST_TYPE_OSMDB  0
#define OSMDB_REQUEST_TYPE_SEARCH 1

static int osmdb_parseY(char* s, int* type, int* y)
{
	*y = (int) strtol(s, NULL, 0);
	return 1;
}

static int osmdb_parseX(char* s, int* type, int* x, int* y)
{
	// find the pattern
	char* p = strchr(s, '/');
	if(p == NULL)
	{
		return 0;
	}
	*p = '\0';
	p = &(p[1]);

	// parse x
	*x = (int) strtol(s, NULL, 0);

	return osmdb_parseY(p, type, y);
}

static int osmdb_parseZoom(char* s, int* type,
                           int* zoom, int* x, int* y)
{
	// find the pattern
	char* p = strchr(s, '/');
	if(p == NULL)
	{
		return 0;
	}
	*p = '\0';
	p = &(p[1]);

	// parse the zoom
	*zoom = (int) strtol(s, NULL, 0);

	return osmdb_parseX(p, type, x, y);
}

static int
osmdb_parseSearch(char* s, char* search)
{
	// initialize search string
	snprintf(search, 256, "%s", s);

	if(search[0] == '\0')
	{
		return 0;
	}

	int idx = 0;
	while(search[idx] != '\0')
	{
		if(((search[idx] >= 'a') && (search[idx] <= 'z')) ||
		   ((search[idx] >= 'A') && (search[idx] <= 'Z')) ||
		   ((search[idx] >= '0') && (search[idx] <= '9')))
		{
			// accept characters
		}
		else
		{
			// ignore unaccepted characters
			search[idx] = ' ';
		}

		++idx;
	}

	return 1;
}
static int
osmdb_parseRequest(const char* s, int* type,
                   int* zoom, int* x, int* y,
                   char* search)
{
	ASSERT(s);
	ASSERT(type);
	ASSERT(zoom);
	ASSERT(x);
	ASSERT(y);
	ASSERT(search);

	// copy request
	char tmp[256];
	strncpy(tmp, s, 256);
	tmp[255] = '\0';

	// determine the type pattern
	char* p;
	if((p = strstr(tmp, "/osmdbv4/")) && (p == tmp))
	{
		*type  = OSMDB_REQUEST_TYPE_OSMDB;
		p = &(p[9]);
		if(osmdb_parseZoom(p, type, zoom, x, y) == 0)
		{
			goto failure;
		}
	}
	else if((p = strstr(tmp, "/search/")) && (p == tmp))
	{
		*type  = OSMDB_REQUEST_TYPE_SEARCH;
		p = &(p[8]);
		if(osmdb_parseSearch(p, search) == 0)
		{
			goto failure;
		}
	}
	else
	{
		goto failure;
	}

	// success
	return 1;

	// failure
	failure:
		LOGE("invalid %s", s);
	return 0;
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, const char** argv)
{
	if(argc != 3)
	{
		LOGE("usage: %s file.sqlite3 [SEARCH|TILE]", argv[0]);
		LOGE("SEARCH: /search/foo+bar");
		LOGE("TILE: /osmdbv4/zoom/x/y");
		return EXIT_FAILURE;
	}

	const char* fname   = argv[1];
	const char* request = argv[2];

	int  type;
	int  zoom;
	int  x;
	int  y;
	char search[256];
	if(osmdb_parseRequest(request, &type, &zoom, &x, &y,
	                      search) == 0)
	{
		return EXIT_FAILURE;
	}

	osmdb_database_t* db = osmdb_database_new(fname);
	if(db == NULL)
	{
		return EXIT_FAILURE;
	}

	xml_ostream_t* os = xml_ostream_newGz("out.xml.gz");
	if(os == NULL)
	{
		goto fail_os;
	}

	if(type == OSMDB_REQUEST_TYPE_SEARCH)
	{
		char spellfix[256];
		osmdb_database_spellfix(db, search, spellfix);
		osmdb_database_search(db, spellfix, os);
	}
	else if(type == OSMDB_REQUEST_TYPE_OSMDB)
	{
		osmdb_database_tile(db, zoom, x, y, os);
	}

	xml_ostream_delete(&os);
	osmdb_database_delete(&db);

	// success
	return EXIT_SUCCESS;

	// failure
	fail_os:
		osmdb_database_delete(&db);
	return EXIT_FAILURE;
}

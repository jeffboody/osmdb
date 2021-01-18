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
#include "osmdb/index/osmdb_index.h"
#include "terrain/terrain_util.h"

/***********************************************************
* private                                                  *
***********************************************************/

static int osmdb_parseY(char* s, int* y)
{
	*y = (int) strtol(s, NULL, 0);
	return 1;
}

static int osmdb_parseX(char* s, int* x, int* y)
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

	return osmdb_parseY(p, y);
}

static int osmdb_parseZoom(char* s,
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

	return osmdb_parseX(p, x, y);
}

static int
osmdb_parseRequest(const char* s,
                   int* zoom, int* x, int* y)
{
	ASSERT(s);
	ASSERT(zoom);
	ASSERT(x);
	ASSERT(y);

	// copy request
	char tmp[256];
	strncpy(tmp, s, 256);
	tmp[255] = '\0';

	// determine the type pattern
	char* p;
	if((p = strstr(tmp, "/osmdbv4/")) && (p == tmp))
	{
		p = &(p[9]);
		if(osmdb_parseZoom(p, zoom, x, y) == 0)
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
		LOGE("usage: %s file.sqlite3 [TILE]", argv[0]);
		LOGE("TILE: /osmdbv4/zoom/x/y");
		return EXIT_FAILURE;
	}

	const char* fname   = argv[1];
	const char* request = argv[2];

	int  zoom;
	int  x;
	int  y;
	if(osmdb_parseRequest(request, &zoom, &x, &y) == 0)
	{
		return EXIT_FAILURE;
	}

	osmdb_index_t* index = osmdb_index_new(fname);
	if(index == NULL)
	{
		return EXIT_FAILURE;
	}

	xml_ostream_t* os = xml_ostream_newGz("out.xml.gz");
	if(os == NULL)
	{
		goto fail_os;
	}

	osmdb_index_tile(index, zoom, x, y, os);

	xml_ostream_delete(&os);
	osmdb_index_delete(&index);

	// success
	return EXIT_SUCCESS;

	// failure
	fail_os:
		osmdb_index_delete(&index);
	return EXIT_FAILURE;
}

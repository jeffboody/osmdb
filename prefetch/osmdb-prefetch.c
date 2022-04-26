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
#include "libbfs/bfs_file.h"
#include "libbfs/bfs_util.h"
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

#define NZOOM 7
const int ZOOM_LEVEL[] =
{
	3, 5, 7, 9, 11, 13, 15
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
	bfs_file_t*    cache;
} osmdb_prefetch_t;

/***********************************************************
* private                                                  *
***********************************************************/

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

	int           ret  = 0;
	size_t        size = 0;
	osmdb_tile_t* tile;
	tile = osmdb_tiler_make(self->tiler, 0,
	                        zoom, x, y, &size);
	if(tile && (size <= INT_MAX))
	{
		char name[256];
		snprintf(name, 256, "%i/%i/%i", zoom, x, y);
		ret = bfs_file_blobSet(self->cache, name, size,
		                       (const void*) tile);
	}

	osmdb_tile_delete(&tile);

	return ret;
}

static int
osmdb_prefetch_tile(osmdb_prefetch_t* self,
                    int zoom, int x, int y)
{
	ASSERT(self);

	if(osmdb_prefetch_make(self, zoom, x, y) == 0)
	{
		// ignore failures
		printf("[PF] %i/%i/%i failed\n", zoom, x, y);
	}

	// update prefetch state
	if((self->count % 10000) == 0)
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
	if((zoom == 3)  || (zoom == 5)  ||
	   (zoom == 7)  || (zoom == 9)  ||
	   (zoom == 11) || (zoom == 13) ||
	   (zoom == 15))
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
			latT  = US_LATT;
			lonL  = US_LONL;
			latB  = US_LATB;
			lonR  = US_LONR;
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
	self->total += osmdb_prefetch_range(self, 3);
	self->total += osmdb_prefetch_range(self, 5);
	self->total += osmdb_prefetch_range(self, 7);
	self->total += osmdb_prefetch_range(self, 9);
	self->total += osmdb_prefetch_range(self, 11);
	self->total += osmdb_prefetch_range(self, 13);
	self->total += osmdb_prefetch_range(self, 15);

	if(bfs_util_initialize() == 0)
	{
		goto fail_init;
	}

	self->tiler = osmdb_tiler_new(fname_index, 1, smem);
	if(self->tiler == NULL)
	{
		goto fail_tiler;
	}

	self->cache = bfs_file_open(fname_cache, 1,
	                            BFS_MODE_STREAM);
	if(self->cache == NULL)
	{
		goto fail_cache;
	}

	char pa[256];
	char bounds[256];
	char cs[256];
	snprintf(pa, 256, "%s", "zoom/x/y");
	snprintf(bounds, 256, "%lf %lf %lf %lf",
	         latT, lonL, latB, lonR);
	snprintf(cs, 256, "%" PRId64, self->tiler->changeset);
	if((bfs_file_attrSet(self->cache, "name", "osmdbv6") == 0) ||
	   (bfs_file_attrSet(self->cache, "pattern", pa)     == 0) ||
	   (bfs_file_attrSet(self->cache, "ext", "osmdb")    == 0) ||
	   (bfs_file_attrSet(self->cache, "bounds", bounds)  == 0) ||
	   (bfs_file_attrSet(self->cache, "zmin", "3")       == 0) ||
	   (bfs_file_attrSet(self->cache, "zmax", "15")      == 0) ||
	   (bfs_file_attrSet(self->cache, "changeset", cs)   == 0))
	{
		goto fail_attr;
	}

	if(osmdb_prefetch_tiles(self, 0, 0, 0) == 0)
	{
		goto fail_run;
	}

	bfs_file_close(&self->cache);
	osmdb_tiler_delete(&self->tiler);
	bfs_util_shutdown();

	// success
	LOGI("SUCCESS");
	return EXIT_SUCCESS;

	// failure
	fail_run:
	fail_attr:
		bfs_file_close(&self->cache);
	fail_cache:
		osmdb_tiler_delete(&self->tiler);
	fail_tiler:
		bfs_util_shutdown();
	fail_init:
	{
		FREE(self);
		LOGE("FAILURE");
	}
	return ret;
}

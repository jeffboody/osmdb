/*
 * Copyright (c) 2019 Jeff Boody
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
#include <assert.h>
#include <string.h>
#include <math.h>

#include "terrain/terrain_tile.h"
#include "osmdb/osmdb_index.h"
#include "osmdb/osmdb_node.h"
#include "osmdb/osmdb_way.h"
#include "osmdb/osmdb_relation.h"
#include "osmdb/osmdb_util.h"
#include "a3d/a3d_timestamp.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* private                                                  *
***********************************************************/

const char* path_terrain = NULL;

double stats_tiles = 0.0;

static int osmdb_makeTile(osmdb_index_t* index,
                          int zoom, int x, int y)
{
	assert(index);

	stats_tiles += 1.0;
	if(fmod(stats_tiles, 10000.0) == 0.0)
	{
		LOGI("[T] %0.0lf", stats_tiles);
		osmdb_index_stats(index);
	}

	char gzname[256];
	snprintf(gzname, 256, "%s/osmdb/%i/%i/%i.xml.gz",
	         index->base, zoom, x, y);

	if(osmdb_fileExists(gzname))
	{
		return 1;
	}

	if(osmdb_mkdir(gzname) == 0)
	{
		return 0;
	}

	xml_ostream_t* os = xml_ostream_newGz(gzname);
	if(os == NULL)
	{
		return 0;
	}

	if(osmdb_index_makeTile(index, zoom, x, y, os) == 0)
	{
		goto fail_make;
	}

	// succcess
	int ret = xml_ostream_complete(os);
	xml_ostream_delete(&os);
	return ret;

	// failure;
	fail_make:
		xml_ostream_delete(&os);
	return 0;
}

static int osmdb_makeTileR(osmdb_index_t* index,
                           int zoom, int x, int y)
{
	assert(index);

	if(zoom < 3)
	{
		// skip dummy nodes
		int ret = 1;
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x, 2*y + 1);
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x, 2*y);
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x + 1, 2*y + 1);
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x + 1, 2*y);
		return ret;
	}
	else if(zoom == 15)
	{
		// end recursion
		return 1;
	}

	// make tiles at valid zoom levels
	if((zoom == 5) ||
	   (zoom == 8) ||
	   (zoom == 11) ||
	   (zoom == 14))
	{
		if(osmdb_makeTile(index, zoom, x, y) == 0)
		{
			return 0;
		}
	}

	// get flags for next LOD
	short min   = 0;
	short max   = 0;
	int   flags = 0;
	if(terrain_tile_header(path_terrain,
                           x/8, y/8, zoom - 3,
                           &min, &max,
                           &flags) == 0)
	{
		return 0;
	}

	// check if next exists
	int nx = (x%8)/4;
	int ny = (y%8)/4;
	if(nx && ny)
	{
		flags &= TERRAIN_NEXT_BR;
	}
	else if(nx)
	{
		flags &= TERRAIN_NEXT_TR;
	}
	else if(ny)
	{
		flags &= TERRAIN_NEXT_BL;
	}
	else
	{
		flags &= TERRAIN_NEXT_TL;
	}

	// recursively make tiles
	if(flags)
	{
		int ret = 1;
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x, 2*y + 1);
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x, 2*y);
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x + 1, 2*y + 1);
		ret &= osmdb_makeTileR(index, zoom + 1, 2*x + 1, 2*y);
		return ret;
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, const char** argv)
{
	double t0 = a3d_timestamp();

	if(argc != 3)
	{
		LOGE("%s [terrain-path] [prefix]", argv[0]);
		return EXIT_FAILURE;
	}
	path_terrain = argv[1];

	osmdb_index_t* index;
	index = osmdb_index_new(argv[2]);
	if(index == NULL)
	{
		goto fail_index;
	}

	LOGI("MAKE TILES");
	if(osmdb_makeTileR(index, 0, 0, 0) == 0)
	{
		goto fail_make;
	}

	LOGI("FINISH INDEX");
	LOGI("[T] %0.0lf", stats_tiles);
	if(osmdb_index_delete(&index) == 0)
	{
		goto fail_delete_index;
	}

	// success
	LOGI("SUCCESS dt=%lf", a3d_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_delete_index:
	fail_make:
		osmdb_index_delete(&index);
	fail_index:
		LOGI("FAILURE dt=%lf", a3d_timestamp() - t0);
	return EXIT_FAILURE;
}

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

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libbfs/bfs_util.h"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libsqlite3/sqlite3.h"
#include "osmdb/index/osmdb_index.h"
#include "osmdb/tiler/osmdb_tiler.h"
#include "osmdb/osmdb_util.h"
#include "terrain/terrain_util.h"
#include "texgz/texgz_tex.h"
#include "texgz/texgz_png.h"

/***********************************************************
* private                                                  *
***********************************************************/

static void osmdb_draw(texgz_tex_t* img, short x, short y)
{
	ASSERT(img);

	// convert from tile coords to img coords
	// tl: (0.0, 0.0) => (16383, -16384)
	// br: (1.0, 1.0) => (-16384, 16383)
	// short: -32768 => 32767
	int ix   = ((int) x) + 32768;
	int iy   = 32767 - ((int) y);
	int top  = iy/16 - 2;
	int left = ix/16 - 2;
	texgz_tex_fill(img, top, left, 5, 5, 0xFF0000FF);
}

static texgz_tex_t* osmdb_mkimg(int zoom, int x, int y)
{
	texgz_tex_t* img;
	img = texgz_tex_new(4096, 4096, 4096, 4096,
	                    TEXGZ_UNSIGNED_BYTE, TEXGZ_RGBA,
	                    NULL);
	if(img == NULL)
	{
		return NULL;
	}

	texgz_tex_fill(img, 0, 0, 4095, 4095, 0x000000FF);
	texgz_tex_fill(img, 1023, 1023, 2048, 2048, 0x4C4C4CFF);

	return img;
}

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
	if((p = strstr(tmp, "/osmdbv8/")) && (p == tmp))
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

static int osmdb_relFn(void* priv, osmdb_rel_t* rel)
{
	ASSERT(rel);

	char* name = osmdb_rel_name(rel);
	printf("R: class=%s, flags=0x%X, type=%i, "
	       "count=%i, name=%s\n",
	       osmdb_classCodeToName(rel->class), rel->flags,
	       rel->type, rel->count, name ? name : "NULL");
	printf("     center={%i,%i}, range={%i,%i,%i,%i}\n",
	       (int) rel->center.x, (int) rel->center.y,
	       (int) rel->range.t,  (int) rel->range.l,
	       (int) rel->range.b,  (int) rel->range.r);

	return 1;
}

static int osmdb_memberFn(void* priv, osmdb_way_t* way)
{
	ASSERT(priv);
	ASSERT(way);

	texgz_tex_t* img = (texgz_tex_t*) priv;

	char* name = osmdb_way_name(way);
	printf("   M: class=%s, flags=0x%X, layer=%i, "
	       "count=%i, name=%s\n",
	       osmdb_classCodeToName(way->class), way->flags,
	       way->layer, way->count, name ? name : "NULL");
	printf("      center={%i,%i}, range={%i,%i,%i,%i}\n",
	       (int) way->center.x, (int) way->center.y,
	       (int) way->range.t,  (int) way->range.l,
	       (int) way->range.b,  (int) way->range.r);

	osmdb_point_t* pts = osmdb_way_pts(way);
	if(pts)
	{
		int i = 0;
		for(i = 0; i < way->count; ++i)
		{
			if(i == 0)
			{
				printf("      %i,%i", (int) pts[i].x, (int) pts[i].y);
			}
			else if(i%5 == 0)
			{
				printf("\n      %i,%i", (int) pts[i].x, (int) pts[i].y);
			}
			else
			{
				printf(" | %i,%i", (int) pts[i].x, (int) pts[i].y);
			}

			osmdb_draw(img, pts[i].x, pts[i].y);
		}
		printf("\n");
	}

	return 1;
}

static int osmdb_wayFn(void* priv, osmdb_way_t* way)
{
	ASSERT(priv);
	ASSERT(way);

	texgz_tex_t* img = (texgz_tex_t*) priv;

	char* name = osmdb_way_name(way);
	printf("W: class=%s, flags=0x%X, layer=%i, "
	       "count=%i, name=%s\n",
	       osmdb_classCodeToName(way->class), way->flags,
	       way->layer, way->count, name ? name : "NULL");
	printf("   center={%i,%i}, range={%i,%i,%i,%i}\n",
	       (int) way->center.x, (int) way->center.y,
	       (int) way->range.t,  (int) way->range.l,
	       (int) way->range.b,  (int) way->range.r);

	osmdb_point_t* pts = osmdb_way_pts(way);
	if(pts)
	{
		int i = 0;
		for(i = 0; i < way->count; ++i)
		{
			if(i == 0)
			{
				printf("   %i,%i", (int) pts[i].x, (int) pts[i].y);
			}
			else if(i%5 == 0)
			{
				printf("\n   %i,%i", (int) pts[i].x, (int) pts[i].y);
			}
			else
			{
				printf(" | %i,%i", (int) pts[i].x, (int) pts[i].y);
			}

			osmdb_draw(img, pts[i].x, pts[i].y);
		}
		printf("\n");
	}

	return 1;
}

static int osmdb_nodeFn(void* priv, osmdb_node_t* node)
{
	ASSERT(priv);
	ASSERT(node);

	texgz_tex_t* img = (texgz_tex_t*) priv;

	char* name = osmdb_node_name(node);
	printf("N: class=%s, flags=0x%X, ele=%i, name=%s, pt=%i,%i\n",
	       osmdb_classCodeToName(node->class), node->flags,
	       node->ele, name ? name : "NULL",
	       (int) node->pt.x, (int) node->pt.y);

	osmdb_draw(img, node->pt.x, node->pt.y);

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, const char** argv)
{
	if(argc != 3)
	{
		LOGE("usage: %s file.sqlite3 [TILE]", argv[0]);
		LOGE("TILE: /osmdbv8/zoom/x/y");
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

	// create a base image
	texgz_tex_t* img = osmdb_mkimg(zoom, x, y);
	if(img == NULL)
	{
		return EXIT_FAILURE;
	}

	if(bfs_util_initialize() == 0)
	{
		goto fail_init;
	}

	osmdb_tiler_t* tiler;
	tiler = osmdb_tiler_new(fname, 1, 1.0f);
	if(tiler == NULL)
	{
		goto fail_tiler;
	}

	void*  data;
	size_t size = 0;
	data = (void*)
	       osmdb_tiler_make(tiler, 0, zoom, x, y, &size);
	if(data == NULL)
	{
		goto fail_data;
	}

	char oname[256];
	snprintf(oname, 256, "tile-%i-%i-%i.osmdb", zoom, x, y);

	FILE* f = fopen(oname, "w");
	if(f == NULL)
	{
		LOGE("fopen failed");
		goto fail_open;
	}

	if(fwrite((const void*) data, size, 1, f) != 1)
	{
		LOGE("fwrite failed");
		goto fail_write;
	}

	fclose(f);

	osmdb_tileParser_t parser =
	{
		.priv      = (void*) img,
		.rel_fn    = osmdb_relFn,
		.member_fn = osmdb_memberFn,
		.way_fn    = osmdb_wayFn,
		.node_fn   = osmdb_nodeFn,
	};

	// print header
	osmdb_tile_t* tile = (osmdb_tile_t*) data;
	printf("magic=0x%X\n", tile->magic);
	printf("version=%i\n", tile->version);
	printf("zoom=%i, x=%i, y=%i\n",
	       tile->zoom, tile->x, tile->y);
	printf("changeset=%" PRId64 "\n", tile->changeset);
	printf("count_rels=%i\n", tile->count_rels);
	printf("count_ways=%i\n", tile->count_ways);
	printf("count_nodes=%i\n", tile->count_nodes);

	// print contents
	tile = osmdb_tile_new(size, data, &parser);
	if(tile == NULL)
	{
		goto fail_tile;
	}

	char iname[256];
	snprintf(iname, 256, "img-%i-%i-%i.png",
	         tile->zoom, tile->x, tile->y);
	if(texgz_png_export(img, iname) == 0)
	{
		goto fail_img;
	}

	osmdb_tile_delete(&tile);
	osmdb_tiler_delete(&tiler);
	bfs_util_shutdown();
	texgz_tex_delete(&img);

	size_t memsize = MEMSIZE();
	if(memsize)
	{
		LOGE("memory leak detected");
		MEMINFO();
		return EXIT_FAILURE;
	}

	// success
	return EXIT_SUCCESS;

	// failure
	fail_img:
	fail_tile:
	fail_write:
	fail_open:
		FREE(data);
	fail_data:
		osmdb_tiler_delete(&tiler);
	fail_tiler:
		bfs_util_shutdown();
	fail_init:
		texgz_tex_delete(&img);
	return EXIT_FAILURE;
}

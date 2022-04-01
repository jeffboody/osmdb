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

#ifndef osmdb_tile_H
#define osmdb_tile_H

#include <stdint.h>

#define OSMDB_TILE_MAGIC   0xB00D90DB
#define OSMDB_TILE_VERSION 20211112

typedef struct
{
	short x;
	short y;
} osmdb_point_t;

typedef struct
{
	short t;
	short l;
	short b;
	short r;
} osmdb_range_t;

#define OSMDB_NODE_FLAG_BUILDING        0x0020
#define OSMDB_NODE_FLAG_STATE_CAPITAL   0x0100
#define OSMDB_NODE_FLAG_COUNTRY_CAPITAL 0x0200

typedef struct
{
	int class;
	int flags;
	int ele;

	osmdb_point_t pt;

	int size_name;
	// size_name must be multiple of 4 bytes
	// char name[];
} osmdb_node_t;

char* osmdb_node_name(osmdb_node_t* self);

// note: check type/joinWay if adding flags
// INNER flag is the osmdb_relData_t role
// OUTER is the default role
// other roles are used to define center
#define OSMDB_WAY_FLAG_FORWARD  0x0001
#define OSMDB_WAY_FLAG_REVERSE  0x0002
#define OSMDB_WAY_FLAG_BRIDGE   0x0004
#define OSMDB_WAY_FLAG_TUNNEL   0x0008
#define OSMDB_WAY_FLAG_CUTTING  0x0010
#define OSMDB_WAY_FLAG_BUILDING 0x0020
#define OSMDB_WAY_FLAG_INNER    0x1000

typedef struct
{
	int class;
	int flags;
	int layer;

	osmdb_point_t center;
	osmdb_range_t range;

	int size_name;
	int count;
	// size_name must be multiple of 4 bytes
	// char name[];
	// osmdb_point_t pts[];
} osmdb_way_t;

char*          osmdb_way_name(osmdb_way_t* self);
osmdb_point_t* osmdb_way_pts(osmdb_way_t* self);

#define OSMDB_REL_FLAG_BUILDING 0x0020

typedef struct
{
	int class;
	int flags;
	int type;

	osmdb_point_t center;
	osmdb_range_t range;

	int size_name;
	int count;
	// size_name must be multiple of 4 bytes
	// char name[];
	// osmdb_way_t ways[];
} osmdb_rel_t;

char* osmdb_rel_name(osmdb_rel_t* self);

// tl: (0.0, 0.0) => (16383, -16384)
// br: (1.0, 1.0) => (-16384, 16383)
// short: -32768 => 32767
typedef struct
{
	int     magic;
	int     version;
	int64_t changeset;
	int     zoom;
	int     x;
	int     y;
	int     count_rels;
	int     count_ways;
	int     count_nodes;
	// osmdb_rel_t  rels[];
	// osmdb_way_t  ways[];
	// osmdb_node_t nodes[];
} osmdb_tile_t;

typedef int (*osmdb_tileParser_relFn)(void* priv,
                                      osmdb_rel_t* rel);
typedef int (*osmdb_tileParser_wayFn)(void* priv,
                                      osmdb_way_t* way);
typedef int (*osmdb_tileParser_nodeFn)(void* priv,
                                       osmdb_node_t* node);

typedef struct
{
	void* priv;
	osmdb_tileParser_relFn  rel_fn;
	osmdb_tileParser_wayFn  member_fn;
	osmdb_tileParser_wayFn  way_fn;
	osmdb_tileParser_nodeFn node_fn;
} osmdb_tileParser_t;

osmdb_tile_t* osmdb_tile_new(size_t size, void* data,
                             osmdb_tileParser_t* parser);
void          osmdb_tile_delete(osmdb_tile_t** _self);

#endif

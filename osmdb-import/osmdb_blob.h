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

#ifndef osm_blob_H
#define osm_blob_H

#include <stdint.h>

#define OSMDB_BLOB_SIZE 100

#define OSMDB_BLOB_TYPE_NODE_TILE11  0
#define OSMDB_BLOB_TYPE_NODE_TILE14  1
#define OSMDB_BLOB_TYPE_WAY_TILE11   2
#define OSMDB_BLOB_TYPE_WAY_TILE14   3
#define OSMDB_BLOB_TYPE_REL_TILE11   4
#define OSMDB_BLOB_TYPE_REL_TILE14   5
#define OSMDB_BLOB_TYPE_TILE_COUNT   6 // COUNT
#define OSMDB_BLOB_TYPE_NODE_COORD   6
#define OSMDB_BLOB_TYPE_NODE_INFO    7
#define OSMDB_BLOB_TYPE_WAY_INFO     8
#define OSMDB_BLOB_TYPE_WAY_RANGE    9
#define OSMDB_BLOB_TYPE_WAY_NDS     10
#define OSMDB_BLOB_TYPE_REL_INFO    11
#define OSMDB_BLOB_TYPE_REL_MEMBERS 12
#define OSMDB_BLOB_TYPE_REL_RANGE   13
#define OSMDB_BLOB_TYPE_COUNT       14 // COUNT

typedef struct
{
	int64_t nid;
	double  lat;
	double  lon;
} osmdb_blobNodeCoord_t;

typedef struct
{
	int64_t nid;
	int     class;
	int     ele;
	int     size_name;
	// size_name must be multiple of 4 bytes
	// char name[];
} osmdb_blobNodeInfo_t;

// note: check joinWay if adding flags
#define OSMDB_BLOBWAYINFO_FLAG_FORWARD 0x01
#define OSMDB_BLOBWAYINFO_FLAG_REVERSE 0x02
#define OSMDB_BLOBWAYINFO_FLAG_BRIDGE  0x04
#define OSMDB_BLOBWAYINFO_FLAG_TUNNEL  0x08
#define OSMDB_BLOBWAYINFO_FLAG_CUTTING 0x10

typedef struct
{
	int64_t wid;
	int     class;
	int     layer;
	int     flags;
	int     size_name;
	// size_name must be multiple of 4 bytes
	// char name[];
} osmdb_blobWayInfo_t;

typedef struct
{
	int64_t wid;
	double  latT;
	double  lonL;
	double  latB;
	double  lonR;
} osmdb_blobWayRange_t;

typedef struct
{
	int64_t wid;
	int     count;
	// int64_t nds[];
} osmdb_blobWayNds_t;

#define OSMDB_RELINFO_TYPE_NONE         0
#define OSMDB_RELINFO_TYPE_BOUNDARY     1
#define OSMDB_RELINFO_TYPE_MULTIPOLYGON 2

typedef struct
{
	int64_t rid;
	int     type;
	int     class;
	int     size_name;
	// size_name must be multiple of 4 bytes
	// char name[];
} osmdb_blobRelInfo_t;

#define OSMDB_RELDATA_TYPE_NONE 0
#define OSMDB_RELDATA_TYPE_NODE 1
#define OSMDB_RELDATA_TYPE_WAY  2
#define OSMDB_RELDATA_TYPE_REL  3

typedef struct
{
	int64_t ref;
	int     type;
	int     role;
} osmdb_blobRelData_t;

typedef struct
{
	int64_t rid;
	int     count;
	// osmdb_blobRelData_t data[];
} osmdb_blobRelMembers_t;

typedef struct
{
	int64_t rid;
	double  latT;
	double  lonL;
	double  latB;
	double  lonR;
} osmdb_blobRelRange_t;

typedef struct
{
	int64_t id;
	int     count;
	// int64_t refs[];
} osmdb_blobTile_t;

typedef struct
{
	void* priv;

	union
	{
		osmdb_blobNodeCoord_t*  node_coord;
		osmdb_blobNodeInfo_t*   node_info;
		osmdb_blobWayInfo_t*    way_info;
		osmdb_blobWayRange_t*   way_range;
		osmdb_blobWayNds_t*     way_nds;
		osmdb_blobRelInfo_t*    rel_info;
		osmdb_blobRelMembers_t* rel_members;
		osmdb_blobRelRange_t*   rel_range;
		osmdb_blobTile_t*       tile;
	};
} osmdb_blob_t;

size_t               osmdb_blobNodeCoord_sizeof(osmdb_blobNodeCoord_t* self);
char*                osmdb_blobNodeInfo_name(osmdb_blobNodeInfo_t* self);
size_t               osmdb_blobNodeInfo_sizeof(osmdb_blobNodeInfo_t* self);
char*                osmdb_blobWayInfo_name(osmdb_blobWayInfo_t* self);
size_t               osmdb_blobWayInfo_sizeof(osmdb_blobWayInfo_t* self);
size_t               osmdb_blobWayRange_sizeof(osmdb_blobWayRange_t* self);
int64_t*             osmdb_blobWayNds_nds(osmdb_blobWayNds_t* self);
size_t               osmdb_blobWayNds_sizeof(osmdb_blobWayNds_t* self);
char*                osmdb_blobRelInfo_name(osmdb_blobRelInfo_t* self);
size_t               osmdb_blobRelInfo_sizeof(osmdb_blobRelInfo_t* self);
osmdb_blobRelData_t* osmdb_blobRelMembers_data(osmdb_blobRelMembers_t* self);
size_t               osmdb_blobRelMembers_sizeof(osmdb_blobRelMembers_t* self);
size_t               osmdb_blobRelRange_sizeof(osmdb_blobRelRange_t* self);
int64_t*             osmdb_blobTile_refs(osmdb_blobTile_t* self);
size_t               osmdb_blobTile_sizeof(osmdb_blobTile_t* self);

#endif

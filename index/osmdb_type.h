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

#ifndef osmdb_type_H
#define osmdb_type_H

#include <stdint.h>

#define OSMDB_TYPE_TILEREF_NODE11  0
#define OSMDB_TYPE_TILEREF_NODE14  1
#define OSMDB_TYPE_TILEREF_WAY11   2
#define OSMDB_TYPE_TILEREF_WAY14   3
#define OSMDB_TYPE_TILEREF_REL11   4
#define OSMDB_TYPE_TILEREF_REL14   5
#define OSMDB_TYPE_TILEREF_COUNT   6 // COUNT
#define OSMDB_TYPE_NODECOORD       6
#define OSMDB_TYPE_NODEINFO        7
#define OSMDB_TYPE_WAYINFO         8
#define OSMDB_TYPE_WAYRANGE        9
#define OSMDB_TYPE_WAYNDS         10
#define OSMDB_TYPE_RELINFO        11
#define OSMDB_TYPE_RELMEMBERS     12
#define OSMDB_TYPE_RELRANGE       13
#define OSMDB_TYPE_COUNT          14 // COUNT

typedef struct
{
	int64_t nid;
	double  lat;
	double  lon;
} osmdb_nodeCoord_t;

typedef struct
{
	int64_t nid;
	int     class;
	int     ele;
	int     size_name;
	// size_name must be multiple of 4 bytes
	// char name[];
} osmdb_nodeInfo_t;

// note: check joinWay if adding flags
#define OSMDB_WAYINFO_FLAG_FORWARD 0x01
#define OSMDB_WAYINFO_FLAG_REVERSE 0x02
#define OSMDB_WAYINFO_FLAG_BRIDGE  0x04
#define OSMDB_WAYINFO_FLAG_TUNNEL  0x08
#define OSMDB_WAYINFO_FLAG_CUTTING 0x10

typedef struct
{
	int64_t wid;
	int     class;
	int     layer;
	int     flags;
	int     size_name;
	// size_name must be multiple of 4 bytes
	// char name[];
} osmdb_wayInfo_t;

typedef struct
{
	int64_t wid;
	double  latT;
	double  lonL;
	double  latB;
	double  lonR;
} osmdb_wayRange_t;

typedef struct
{
	int64_t wid;
	int     count;
	// int64_t nds[];
} osmdb_wayNds_t;

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
} osmdb_relInfo_t;

#define OSMDB_RELDATA_TYPE_NONE 0
#define OSMDB_RELDATA_TYPE_NODE 1
#define OSMDB_RELDATA_TYPE_WAY  2
#define OSMDB_RELDATA_TYPE_REL  3

typedef struct
{
	int64_t ref;
	int     type;
	int     role;
} osmdb_relData_t;

typedef struct
{
	int64_t rid;
	int     count;
	// osmdb_relData_t data[];
} osmdb_relMembers_t;

typedef struct
{
	int64_t rid;
	double  latT;
	double  lonL;
	double  latB;
	double  lonR;
} osmdb_relRange_t;

typedef struct
{
	int64_t id;
	int     count;
	// int64_t refs[];
} osmdb_tileRefs_t;

typedef struct osmdb_entry_s osmdb_entry_t;

typedef struct
{
	osmdb_entry_t* entry;

	union
	{
		osmdb_nodeCoord_t*  node_coord;
		osmdb_nodeInfo_t*   node_info;
		osmdb_wayInfo_t*    way_info;
		osmdb_wayRange_t*   way_range;
		osmdb_wayNds_t*     way_nds;
		osmdb_relInfo_t*    rel_info;
		osmdb_relMembers_t* rel_members;
		osmdb_relRange_t*   rel_range;
		osmdb_tileRefs_t*   tile_refs;
	};
} osmdb_handle_t;

size_t           osmdb_nodeCoord_sizeof(osmdb_nodeCoord_t* self);
char*            osmdb_nodeInfo_name(osmdb_nodeInfo_t* self);
size_t           osmdb_nodeInfo_sizeof(osmdb_nodeInfo_t* self);
char*            osmdb_wayInfo_name(osmdb_wayInfo_t* self);
size_t           osmdb_wayInfo_sizeof(osmdb_wayInfo_t* self);
size_t           osmdb_wayRange_sizeof(osmdb_wayRange_t* self);
int64_t*         osmdb_wayNds_nds(osmdb_wayNds_t* self);
size_t           osmdb_wayNds_sizeof(osmdb_wayNds_t* self);
char*            osmdb_relInfo_name(osmdb_relInfo_t* self);
size_t           osmdb_relInfo_sizeof(osmdb_relInfo_t* self);
osmdb_relData_t* osmdb_relMembers_data(osmdb_relMembers_t* self);
size_t           osmdb_relMembers_sizeof(osmdb_relMembers_t* self);
size_t           osmdb_relRange_sizeof(osmdb_relRange_t* self);
int64_t*         osmdb_tileRefs_refs(osmdb_tileRefs_t* self);
size_t           osmdb_tileRefs_sizeof(osmdb_tileRefs_t* self);

#endif

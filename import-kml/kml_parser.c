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
#include <string.h>
#include <strings.h>
#include <math.h>
#include "libxmlstream/xml_istream.h"
#include "kml_parser.h"

#define LOG_TAG "osmdb"
#include "libbfs/bfs_util.h"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "osmdb/osmdb_util.h"
#include "terrain/terrain_util.h"

#define KML_STATE_INIT             0
#define KML_STATE_KML              1
#define KML_STATE_DOCUMENT         2
#define KML_STATE_FOLDER           3
#define KML_STATE_FOLDERNAME       4
#define KML_STATE_PLACEMARK        5
#define KML_STATE_PLACEMARKNAME    6
#define KML_STATE_POLYGON          7
#define KML_STATE_MULTIGEOMETRY    8
#define KML_STATE_OUTERBOUNDARYIS  9
#define KML_STATE_INNERBOUNDARYIS  10
#define KML_STATE_LINEARRING       11
#define KML_STATE_COORDINATES      12
#define KML_STATE_EXTENDEDDATA     13
#define KML_STATE_SCHEMADATA       14
#define KML_STATE_SIMPLEDATA       15

#define KML_SIMPLEDATA_UNKNOWN 0
#define KML_SIMPLEDATA_TYPE    1

#define KML_PARSER_WAY_NDS 64

// protected functions
int osmdb_index_add(osmdb_index_t* self,
                    int type, int64_t id,
                    size_t size, void* data);
int osmdb_index_addTile(osmdb_index_t* self,
                        int type, int64_t major_id,
                        int64_t ref);
void osmdb_nodeInfo_addName(osmdb_nodeInfo_t* self,
                            const char* name);
void osmdb_wayInfo_addName(osmdb_wayInfo_t* self,
                           const char* name);
void osmdb_relInfo_addName(osmdb_relInfo_t* self,
                           const char* name);

typedef struct
{
	double lat;
	double lon;
} kml_coordKey_t;

/***********************************************************
* private                                                  *
***********************************************************/

static int kml_parser_state(kml_parser_t* self)
{
	ASSERT(self);

	int* _state = (int*) cc_list_peekHead(self->list_state);
	if(_state)
	{
		return *_state;
	}

	return KML_STATE_INIT;
}

static int
kml_parser_statePush(kml_parser_t* self, int state)
{
	ASSERT(self);

	int* _state = (int*) MALLOC(sizeof(int));
	if(_state == NULL)
	{
		LOGE("MALLOC failed");
		return KML_STATE_INIT;
	}
	*_state = state;

	if(cc_list_insert(self->list_state, NULL,
	                  (const void*) _state) == NULL)
	{
		LOGE("push failed");
		goto fail_push;
	}

	// success
	return 1;

	// failure
	fail_push:
		FREE(_state);
	return KML_STATE_INIT;
}

static int kml_parser_statePop(kml_parser_t* self)
{
	ASSERT(self);

	cc_listIter_t* iter = cc_list_head(self->list_state);
	if(iter)
	{
		int* _state;
		_state = (int*) cc_list_remove(self->list_state, &iter);

		int state = *_state;
		FREE(_state);

		return state;
	}
	return KML_STATE_INIT;
}

static int clamp(int val, int a, int b)
{
	if(val < a)
	{
		val = a;
	}
	else if(val > b)
	{
		val = b;
	}

	return val;
}

static int
kml_parser_addTileRange(kml_parser_t* self,
                        int type, int64_t ref,
                        double latT, double lonL,
                        double latB, double lonR,
                        int min_zoom)
{
	ASSERT(self);

	if(min_zoom >= 15)
	{
		min_zoom = 15;
	}
	else if(min_zoom >= 13)
	{
		min_zoom = 13;
	}
	else if(min_zoom >= 11)
	{
		min_zoom = 11;
	}
	else if(min_zoom >= 9)
	{
		min_zoom = 9;
	}
	else if(min_zoom >= 7)
	{
		min_zoom = 7;
	}
	else if(min_zoom >= 5)
	{
		min_zoom = 5;
	}
	else if(min_zoom >= 3)
	{
		min_zoom = 3;
	}
	else
	{
		LOGE("invalid min_zoom=%i", min_zoom);
		return 0;
	}

	// elements are defined with zero width but in
	// practice are drawn with non-zero width
	// points/lines so a border is needed to ensure they
	// are not clipped between neighboring tiles
	float border = 1.0f/16.0f;

	// determine the tile type
	int type_way[]  =
	{
		OSMDB_TYPE_TILEREF_WAY15,
		OSMDB_TYPE_TILEREF_WAY13,
		OSMDB_TYPE_TILEREF_WAY11,
		OSMDB_TYPE_TILEREF_WAY9,
		OSMDB_TYPE_TILEREF_WAY7,
		OSMDB_TYPE_TILEREF_WAY5,
		OSMDB_TYPE_TILEREF_WAY3,
	};
	int type_rel[]  =
	{
		OSMDB_TYPE_TILEREF_REL15,
		OSMDB_TYPE_TILEREF_REL13,
		OSMDB_TYPE_TILEREF_REL11,
		OSMDB_TYPE_TILEREF_REL9,
		OSMDB_TYPE_TILEREF_REL7,
		OSMDB_TYPE_TILEREF_REL5,
		OSMDB_TYPE_TILEREF_REL3,
	};
	int* type_array;
	if(type == OSMDB_TYPE_WAYRANGE)
	{
		type_array = type_way;
	}
	else if(type == OSMDB_TYPE_RELRANGE)
	{
		type_array = type_rel;
	}
	else
	{
		LOGE("invalid type=%i", type);
		return 0;
	}

	// add to tiles
	float x0;
	float y0;
	float x1;
	float y1;
	int   ix0;
	int   iy0;
	int   ix1;
	int   iy1;
	int   id;
	int   i        = 0;
	int zoom[]     = { 15,    13,   11,   9,   7,   5,  3, -1 };
	int max_zoom[] = { 1000,  15,   13,   11,  9,   7,  5, -1 };
	int pow2n[]    = { 32768, 8192, 2048, 512, 128, 32, 8, -1 };
	while(min_zoom < max_zoom[i])
	{
		terrain_coord2tile(latT, lonL,
		                   zoom[i], &x0, &y0);
		terrain_coord2tile(latB, lonR,
		                   zoom[i], &x1, &y1);
		ix0 = clamp((int) (x0 - border), 0, pow2n[i] - 1);
		iy0 = clamp((int) (y0 - border), 0, pow2n[i] - 1);
		ix1 = clamp((int) (x1 + border), 0, pow2n[i] - 1);
		iy1 = clamp((int) (y1 + border), 0, pow2n[i] - 1);

		int r;
		int c;
		for(r = iy0; r <= iy1; ++r)
		{
			for(c = ix0; c <= ix1; ++c)
			{
				id = (int64_t) pow2n[i]*r + c;
				if(osmdb_index_addTile(self->index, type_array[i],
				                       id, ref) == 0)
				{
					return 0;
				}
			}
		}

		++i;
	}

	return 1;
}

static int
kml_parser_wayAddSeg(kml_parser_t* self)
{
	ASSERT(self);

	const char* class_name;
	class_name = osmdb_classCodeToName(self->class);

	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style, class_name);
	if(sc && sc->line && self->seg_nds->count)
	{
		osmdb_wayInfo_t way_info =
		{
			.wid   = self->wid,
			.class = self->class
		};

		// add optional name (e.g. for boundary:state ways)
		if(self->folder_class && (self->name[0] != '\0'))
		{
			osmdb_wayInfo_addName(&way_info, self->name);
		}

		size_t size = osmdb_wayInfo_sizeof(&way_info);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_WAYINFO,
		                   self->wid, size,
		                   (void*) &way_info) == 0)
		{
			return 0;
		}

		osmdb_wayRange_t way_range =
		{
			.wid  = self->wid,
			.latT = self->seg_latT,
			.lonL = self->seg_lonL,
			.latB = self->seg_latB,
			.lonR = self->seg_lonR
		};

		size = osmdb_wayRange_sizeof(&way_range);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_WAYRANGE,
		                   self->wid, size,
		                   (void*) &way_range) == 0)
		{
			return 0;
		}

		self->seg_nds->wid = self->wid;
		size = osmdb_wayNds_sizeof(self->seg_nds);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_WAYNDS,
		                   self->wid,
		                   size, (void*) self->seg_nds) == 0)
		{
			return 0;
		}

		int min_zoom = sc->line->min_zoom;
		if(kml_parser_addTileRange(self,
		                           OSMDB_TYPE_WAYRANGE,
		                           way_range.wid,
		                           way_range.latT, way_range.lonL,
		                           way_range.latB, way_range.lonR,
		                           min_zoom) == 0)
		{
			return 0;
		}
	}

	self->wid            -= 1;
	self->seg_nds->count  = 0;
	self->seg_latT        = 0.0;
	self->seg_lonL        = 0.0;
	self->seg_latB        = 0.0;
	self->seg_lonR        = 0.0;

	return 1;
}

static void
kml_parser_wayAddNd(kml_parser_t* self, osmdb_nodeCoord_t* node_coord)
{
	ASSERT(self);

	// update bounding boxes
	if(self->way_nds)
	{
		if(node_coord->lat > self->way_latT)
		{
			self->way_latT = node_coord->lat;
		}
		if(node_coord->lon < self->way_lonL)
		{
			self->way_lonL = node_coord->lon;
		}
		if(node_coord->lat < self->way_latB)
		{
			self->way_latB = node_coord->lat;
		}
		if(node_coord->lon > self->way_lonR)
		{
			self->way_lonR = node_coord->lon;
		}
	}
	else
	{
		self->way_latT = node_coord->lat;
		self->way_lonL = node_coord->lon;
		self->way_latB = node_coord->lat;
		self->way_lonR = node_coord->lon;
	}

	if(self->seg_nds->count)
	{
		if(node_coord->lat > self->seg_latT)
		{
			self->seg_latT = node_coord->lat;
		}
		if(node_coord->lon < self->seg_lonL)
		{
			self->seg_lonL = node_coord->lon;
		}
		if(node_coord->lat < self->seg_latB)
		{
			self->seg_latB = node_coord->lat;
		}
		if(node_coord->lon > self->seg_lonR)
		{
			self->seg_lonR = node_coord->lon;
		}
	}
	else
	{
		self->seg_latT = node_coord->lat;
		self->seg_lonL = node_coord->lon;
		self->seg_latB = node_coord->lat;
		self->seg_lonR = node_coord->lon;
	}

	// append to seg_nds
	int64_t* nds = osmdb_wayNds_nds(self->seg_nds);
	nds[self->seg_nds->count] = node_coord->nid;
	++self->way_nds;
	++self->seg_nds->count;
}

static int
kml_parser_parseNode(kml_parser_t* self, int min_zoom,
                     char* s)
{
	ASSERT(self);
	ASSERT(s);

	cc_map_t* map_node_coords;
	if(min_zoom >= 15)
	{
		map_node_coords = self->map_node_coords15;
	}
	else if(min_zoom >= 13)
	{
		map_node_coords = self->map_node_coords13;
	}
	else if(min_zoom >= 11)
	{
		map_node_coords = self->map_node_coords11;
	}
	else if(min_zoom >= 9)
	{
		map_node_coords = self->map_node_coords9;
	}
	else if(min_zoom >= 7)
	{
		map_node_coords = self->map_node_coords7;
	}
	else if(min_zoom >= 5)
	{
		map_node_coords = self->map_node_coords5;
	}
	else if(min_zoom >= 3)
	{
		map_node_coords = self->map_node_coords3;
	}
	else
	{
		LOGE("invalid min_zoom=%i", min_zoom);
		return 0;
	}


	int n = 1;
	int i = 0;
	while(1)
	{
		if(s[i] == '\0')
		{
			break;
		}
		else if(s[i] == ',')
		{
			++n;
		}

		++i;
	}

	double lat;
	double lon;
	float  alt;
	if(n == 2)
	{
		if(sscanf(s, "%lf,%lf", &lon, &lat) != 2)
		{
			LOGE("invalid %s", s);
			return 0;
		}
	}
	else if(n == 3)
	{
		if(sscanf(s, "%lf,%lf,%f", &lon, &lat, &alt) != 3)
		{
			LOGE("invalid %s", s);
			return 0;
		}
	}
	else
	{
		LOGE("invalid %s", s);
		return 0;
	}

	kml_coordKey_t key =
	{
		.lat = lat,
		.lon = lon
	};

	osmdb_nodeCoord_t* node_coord;

	cc_mapIter_t* miter;
	miter = cc_map_findp(map_node_coords,
	                     sizeof(kml_coordKey_t), &key);
	if(miter == NULL)
	{
		node_coord = (osmdb_nodeCoord_t*)
		             MALLOC(sizeof(osmdb_nodeCoord_t));
		if(node_coord == NULL)
		{
			LOGE("MALLOC failed");
			return 0;
		}
		node_coord->nid = self->nid;
		node_coord->lat = lat;
		node_coord->lon = lon;

		if(cc_map_addp(map_node_coords,
		               (const void*) node_coord,
		               sizeof(kml_coordKey_t), &key) == NULL)
		{
			FREE(node_coord);
			return 0;
		}

		// advance the next node_coord nid
		self->nid -= 1;
	}
	else
	{
		node_coord = (osmdb_nodeCoord_t*) cc_map_val(miter);
	}

	kml_parser_wayAddNd(self, node_coord);

	// split way to avoid very large ways
	if(self->seg_nds->count >= KML_PARSER_WAY_NDS)
	{
		if(kml_parser_wayAddSeg(self) == 0)
		{
			return 0;
		}

		kml_parser_wayAddNd(self, node_coord);
	}

	return 1;
}

static int
kml_parser_parseContent(kml_parser_t* self, char* content)
{
	ASSERT(self);
	ASSERT(content);

	const char* class_name;
	class_name = osmdb_classCodeToName(self->class);

	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style, class_name);
	if((sc == NULL) || (sc->line == NULL))
	{
		LOGE("invalid class_name=%s", class_name);
		return 0;
	}

	int min_zoom = sc->line->min_zoom;

	int   idx = 0;
	char* s   = content;
	while(1)
	{
		char c = content[idx];
		if(c == '\0')
		{
			break;
		}
		else if(c == ' ')
		{
			content[idx] = '\0';

			if(kml_parser_parseNode(self, min_zoom, s) == 0)
			{
				return 0;
			}

			++idx;
			s = &content[idx];
			continue;
		}

		++idx;
	}

	return kml_parser_parseNode(self, min_zoom, s);
}

static int
kml_parser_beginKml(kml_parser_t* self, int line,
                    const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_KML);
}

static int
kml_parser_endKml(kml_parser_t* self, int line,
                  const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginDocument(kml_parser_t* self, int line,
                         const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_DOCUMENT);
}

static int
kml_parser_endDocument(kml_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginFolder(kml_parser_t* self, int line,
                       const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_FOLDER);
}

static int
kml_parser_endFolder(kml_parser_t* self, int line,
                     const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->folder_class = 0;
	self->class        = 0;

	return kml_parser_statePop(self);
}

static int
kml_parser_beginFolderName(kml_parser_t* self, int line,
                           const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_FOLDERNAME);
}

static int
kml_parser_endFolderName(kml_parser_t* self, int line,
                         const char* content)
{
	// content may be NULL
	ASSERT(self);

	if(content)
	{
		// boundary:state is a custom class for state boundaries
		// which are imported from cb_2018_us_state_500k.kml
		if(strcmp(content, "cb_2018_us_state_500k") == 0)
		{
			self->class = osmdb_classNameToCode("boundary:state");

			self->folder_class = self->class;
		}
	}
	return kml_parser_statePop(self);
}

static int
kml_parser_beginPlacemark(kml_parser_t* self, int line,
                          const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_PLACEMARK);
}

static int
kml_parser_addTileCoord(kml_parser_t* self,
                        int64_t ref,
                        double lat, double lon,
                        int min_zoom)
{
	ASSERT(self);

	if(min_zoom >= 15)
	{
		min_zoom = 15;
	}
	else if(min_zoom >= 13)
	{
		min_zoom = 13;
	}
	else if(min_zoom >= 11)
	{
		min_zoom = 11;
	}
	else if(min_zoom >= 9)
	{
		min_zoom = 9;
	}
	else if(min_zoom >= 7)
	{
		min_zoom = 7;
	}
	else if(min_zoom >= 5)
	{
		min_zoom = 5;
	}
	else if(min_zoom >= 3)
	{
		min_zoom = 3;
	}
	else
	{
		LOGE("invalid min_zoom=%i", min_zoom);
		return 0;
	}

	float   x;
	float   y;
	int64_t id;

	int ix;
	int iy;
	int i          = 0;
	int zoom[]     = { 15,    13,   11,   9,   7,   5,  3, -1 };
	int max_zoom[] = { 1000,  15,   13,   11,  9,   7,  5, -1 };
	int pow2n[]    = { 32768, 8192, 2048, 512, 128, 32, 8, -1 };

	int type_array[]  =
	{
		OSMDB_TYPE_TILEREF_NODE15,
		OSMDB_TYPE_TILEREF_NODE13,
		OSMDB_TYPE_TILEREF_NODE11,
		OSMDB_TYPE_TILEREF_NODE9,
		OSMDB_TYPE_TILEREF_NODE7,
		OSMDB_TYPE_TILEREF_NODE5,
		OSMDB_TYPE_TILEREF_NODE3,
	};

	while(min_zoom < max_zoom[i])
	{
		terrain_coord2tile(lat, lon,
		                   zoom[i], &x, &y);
		ix = (int) x;
		iy = (int) y;
		id = (int64_t) pow2n[i]*iy + ix;
		if(osmdb_index_addTile(self->index, type_array[i],
		                       id, ref) == 0)
		{
			return 0;
		}

		++i;
	}

	return 1;
}

static int
kml_parser_endPlacemark(kml_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

	const char* class_name;
	class_name = osmdb_classCodeToName(self->class);

	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style, class_name);

	if(sc && sc->point &&
	   self->way_nds && self->class &&
	   (self->name[0] != '\0'))
	{
		osmdb_nodeCoord_t node_coord =
		{
			.nid = self->nid,
			.lat = self->way_latB +
			       (self->way_latT - self->way_latB)/2.0,
			.lon = self->way_lonL +
			       (self->way_lonR - self->way_lonL)/2.0
		};

		size_t size = osmdb_nodeCoord_sizeof(&node_coord);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_NODECOORD,
		                   self->nid, size,
		                   (void*) &node_coord) == 0)
		{
			return 0;
		}

		self->node_info->nid   = self->nid;
		self->node_info->class = self->class;
		osmdb_nodeInfo_addName(self->node_info, self->name);

		size = osmdb_nodeInfo_sizeof(self->node_info);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_NODEINFO,
		                   self->nid, size,
		                   (void*) self->node_info) == 0)
		{
			return 0;
		}

		int min_zoom = sc->point->min_zoom;
		if(kml_parser_addTileCoord(self, node_coord.nid,
		                           node_coord.lat, node_coord.lon,
		                           min_zoom) == 0)
		{
			return 0;
		}

		// advance the next node id
		self->nid -= 1;
	}

	snprintf(self->name, 256, "%s", "");
	self->class = self->folder_class;

	self->way_nds  = 0;
	self->way_latT = 0.0;
	self->way_lonL = 0.0;
	self->way_latB = 0.0;
	self->way_lonR = 0.0;


	return kml_parser_statePop(self);
}

static int
kml_parser_beginPlacemarkName(kml_parser_t* self, int line,
                              const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_PLACEMARKNAME);
}

static int
kml_parser_endPlacemarkName(kml_parser_t* self, int line,
                            const char* content)
{
	// content may be NULL
	ASSERT(self);

	if(content)
	{
		snprintf(self->name, 256, "%s", content);
	}
	return kml_parser_statePop(self);
}

static int
kml_parser_beginPolygon(kml_parser_t* self, int line,
                        const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_POLYGON);
}

static int
kml_parser_endPolygon(kml_parser_t* self, int line,
                      const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginMultiGeometry(kml_parser_t* self, int line,
                              const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_MULTIGEOMETRY);
}

static int
kml_parser_endMultiGeometry(kml_parser_t* self, int line,
                            const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginOuterBoundaryIs(kml_parser_t* self, int line,
                                const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_OUTERBOUNDARYIS);
}

static int
kml_parser_endOuterBoundaryIs(kml_parser_t* self, int line,
                              const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginInnerBoundaryIs(kml_parser_t* self, int line,
                                const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_INNERBOUNDARYIS);
}

static int
kml_parser_endInnerBoundaryIs(kml_parser_t* self, int line,
                              const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginLinearRing(kml_parser_t* self, int line,
                           const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_LINEARRING);
}

static int
kml_parser_endLinearRing(kml_parser_t* self, int line,
                         const char* content)
{
	// content may be NULL
	ASSERT(self);

	if(kml_parser_wayAddSeg(self) == 0)
	{
		return 0;
	}

	return kml_parser_statePop(self);
}

static int
kml_parser_beginCoordinates(kml_parser_t* self, int line,
                            const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_COORDINATES);
}

static int
kml_parser_endCoordinates(kml_parser_t* self, int line,
                          const char* content)
{
	// content may be NULL
	ASSERT(self);

	if(content)
	{
		int len = strlen(content) + 1;
		char* copy = MALLOC(len*sizeof(char));
		if(copy == NULL)
		{
			LOGE("MALLOC failed");
			return 0;
		}

		snprintf(copy, len, "%s", content);
		if(kml_parser_parseContent(self, copy) == 0)
		{
			FREE(copy);
			return 0;
		}
		FREE(copy);
	}

	return kml_parser_statePop(self);
}

static int
kml_parser_beginExtendedData(kml_parser_t* self, int line,
                             const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_EXTENDEDDATA);
}

static int
kml_parser_endExtendedData(kml_parser_t* self, int line,
                           const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginSchemaData(kml_parser_t* self, int line,
                           const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	return kml_parser_statePush(self,
	                            KML_STATE_SCHEMADATA);
}

static int
kml_parser_endSchemaData(kml_parser_t* self, int line,
                         const char* content)
{
	// content may be NULL
	ASSERT(self);

	return kml_parser_statePop(self);
}

static int
kml_parser_beginSimpleData(kml_parser_t* self, int line,
                           const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strstr(atts[idx0], "name"))
		{
			if(strstr(atts[idx1], "Designatio") ||
			   strstr(atts[idx1], "PROPOSAL"))
			{
				self->simpledata = KML_SIMPLEDATA_TYPE;
			}
		}

		idx0 += 2;
		idx1 += 2;
	}

	return kml_parser_statePush(self,
	                            KML_STATE_SIMPLEDATA);
}

static int
kml_parser_endSimpleData(kml_parser_t* self, int line,
                         const char* content)
{
	// content may be NULL
	ASSERT(self);

	if(content)
	{
		if((self->class == 0) &&
		   (self->simpledata == KML_SIMPLEDATA_TYPE))
		{
			if(strcasecmp(content, "Wilderness") == 0)
			{
				self->class = osmdb_classNameToCode("core:wilderness");
			}
			else if(strcasecmp(content, "Special Management Area") == 0)
			{
				self->class = osmdb_classNameToCode("core:special");
			}
			else if(strcasecmp(content, "Mineral Withdrawal") == 0)
			{
				self->class = osmdb_classNameToCode("core:mineral");
			}
			else if(strcasecmp(content, "National Recreation Area") == 0)
			{
				self->class = osmdb_classNameToCode("core:recreation");
			}
			else if(strcasecmp(content, "National Historic Landscape") == 0)
			{
				self->class = osmdb_classNameToCode("core:historic");
			}
			else if(strcasecmp(content, "Coal Mine Methane Capture Areas") == 0)
			{
				self->class = osmdb_classNameToCode("core:coal_methane");
			}
			else if(strcasecmp(content, "Proposed Wilderness") == 0)
			{
				self->class = osmdb_classNameToCode("rec:wilderness");
			}
			else if(strcasecmp(content, "Proposed Special Management A*") == 0)
			{
				self->class = osmdb_classNameToCode("rec:special");
			}
			else if(strcasecmp(content, "Proposed Mineral Withdrawal A*") == 0)
			{
				self->class = osmdb_classNameToCode("rec:mineral");
			}
			else
			{
				LOGW("unknown line=%i, content=%s", line, content);
			}
		}
	}

	self->simpledata = KML_SIMPLEDATA_UNKNOWN;

	return kml_parser_statePop(self);
}

static int kml_parser_start(void* priv,
                            int line, float progress,
                            const char* name,
                            const char** atts)
{
	ASSERT(priv);
	ASSERT(name);
	ASSERT(atts);

	kml_parser_t* self = (kml_parser_t*) priv;

	int state = kml_parser_state(self);
	if(self->discard)
	{
		// discard unknown nodes recursively
		++self->discard;
		return 1;
	}
	else if(state == KML_STATE_INIT)
	{
		if(strcasecmp(name, "kml") == 0)
		{
			return kml_parser_beginKml(self, line, atts);
		}
	}
	else if(state == KML_STATE_KML)
	{
		if(strcasecmp(name, "Document") == 0)
		{
			return kml_parser_beginDocument(self, line, atts);
		}
	}
	else if(state == KML_STATE_DOCUMENT)
	{
		if(strcasecmp(name, "Folder") == 0)
		{
			return kml_parser_beginFolder(self, line, atts);
		}
	}
	else if(state == KML_STATE_FOLDER)
	{
		if(strcasecmp(name, "Placemark") == 0)
		{
			return kml_parser_beginPlacemark(self, line, atts);
		}
		else if(strcasecmp(name, "name") == 0)
		{
			return kml_parser_beginFolderName(self, line, atts);
		}
	}
	else if(state == KML_STATE_PLACEMARK)
	{
		if(strcasecmp(name, "name") == 0)
		{
			return kml_parser_beginPlacemarkName(self, line, atts);
		}
		else if(strcasecmp(name, "Polygon") == 0)
		{
			return kml_parser_beginPolygon(self, line, atts);
		}
		else if(strcasecmp(name, "MultiGeometry") == 0)
		{
			return kml_parser_beginMultiGeometry(self, line, atts);
		}
		else if(strcasecmp(name, "ExtendedData") == 0)
		{
			return kml_parser_beginExtendedData(self, line, atts);
		}
	}
	else if(state == KML_STATE_MULTIGEOMETRY)
	{
		if(strcasecmp(name, "Polygon") == 0)
		{
			return kml_parser_beginPolygon(self, line, atts);
		}
	}
	else if(state == KML_STATE_POLYGON)
	{
		if(strcasecmp(name, "outerBoundaryIs") == 0)
		{
			return kml_parser_beginOuterBoundaryIs(self, line, atts);
		}
		else if(strcasecmp(name, "innerBoundaryIs") == 0)
		{
			return kml_parser_beginInnerBoundaryIs(self, line, atts);
		}
	}
	else if(state == KML_STATE_OUTERBOUNDARYIS)
	{
		if(strcasecmp(name, "LinearRing") == 0)
		{
			return kml_parser_beginLinearRing(self, line, atts);
		}
	}
	else if(state == KML_STATE_INNERBOUNDARYIS)
	{
		if(strcasecmp(name, "LinearRing") == 0)
		{
			return kml_parser_beginLinearRing(self, line, atts);
		}
	}
	else if(state == KML_STATE_LINEARRING)
	{
		if(strcasecmp(name, "coordinates") == 0)
		{
			return kml_parser_beginCoordinates(self, line, atts);
		}
	}
	else if(state == KML_STATE_EXTENDEDDATA)
	{
		if(strcasecmp(name, "SchemaData") == 0)
		{
			return kml_parser_beginSchemaData(self, line, atts);
		}
	}
	else if(state == KML_STATE_SCHEMADATA)
	{
		if(strcasecmp(name, "SimpleData") == 0)
		{
			return kml_parser_beginSimpleData(self, line, atts);
		}
	}

	// discard unknown nodes
	++self->discard;
	return 1;
}

static int kml_parser_end(void* priv,
                          int line, float progress,
                          const char* name,
                          const char* content)
{
	// content may be NULL
	ASSERT(priv);
	ASSERT(name);

	kml_parser_t* self = (kml_parser_t*) priv;

	int state = kml_parser_state(self);
	if(self->discard)
	{
		--self->discard;
		return 1;
	}
	else if(state == KML_STATE_KML)
	{
		return kml_parser_endKml(self, line, content);
	}
	else if(state == KML_STATE_DOCUMENT)
	{
		return kml_parser_endDocument(self, line, content);
	}
	else if(state == KML_STATE_FOLDER)
	{
		return kml_parser_endFolder(self, line, content);
	}
	else if(state == KML_STATE_FOLDERNAME)
	{
		return kml_parser_endFolderName(self, line, content);
	}
	else if(state == KML_STATE_PLACEMARK)
	{
		return kml_parser_endPlacemark(self, line, content);
	}
	else if(state == KML_STATE_PLACEMARKNAME)
	{
		return kml_parser_endPlacemarkName(self, line, content);
	}
	else if(state == KML_STATE_MULTIGEOMETRY)
	{
		return kml_parser_endMultiGeometry(self, line, content);
	}
	else if(state == KML_STATE_POLYGON)
	{
		return kml_parser_endPolygon(self, line, content);
	}
	else if(state == KML_STATE_OUTERBOUNDARYIS)
	{
		return kml_parser_endOuterBoundaryIs(self, line, content);
	}
	else if(state == KML_STATE_INNERBOUNDARYIS)
	{
		return kml_parser_endInnerBoundaryIs(self, line, content);
	}
	else if(state == KML_STATE_LINEARRING)
	{
		return kml_parser_endLinearRing(self, line, content);
	}
	else if(state == KML_STATE_COORDINATES)
	{
		return kml_parser_endCoordinates(self, line, content);
	}
	else if(state == KML_STATE_EXTENDEDDATA)
	{
		return kml_parser_endExtendedData(self, line, content);
	}
	else if(state == KML_STATE_SCHEMADATA)
	{
		return kml_parser_endSchemaData(self, line, content);
	}
	else if(state == KML_STATE_SIMPLEDATA)
	{
		return kml_parser_endSimpleData(self, line, content);
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

/***********************************************************
* public                                                   *
***********************************************************/

kml_parser_t*
kml_parser_new(const char* style,
               const char* db_name)
{
	ASSERT(style);
	ASSERT(db_name);

	kml_parser_t* self = (kml_parser_t*)
	                     CALLOC(1, sizeof(kml_parser_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	// -1 is reserved for an invalid ID
	self->nid = -2;
	self->wid = -2;

	snprintf(self->name, 256, "%s", "");

	self->list_state = cc_list_new();
	if(self->list_state == NULL)
	{
		goto fail_list_state;
	}

	self->map_node_coords3 = cc_map_new();
	if(self->map_node_coords3 == NULL)
	{
		goto fail_map_node_coords3;
	}

	self->map_node_coords5 = cc_map_new();
	if(self->map_node_coords5 == NULL)
	{
		goto fail_map_node_coords5;
	}

	self->map_node_coords7 = cc_map_new();
	if(self->map_node_coords7 == NULL)
	{
		goto fail_map_node_coords7;
	}

	self->map_node_coords9 = cc_map_new();
	if(self->map_node_coords9 == NULL)
	{
		goto fail_map_node_coords9;
	}

	self->map_node_coords11 = cc_map_new();
	if(self->map_node_coords11 == NULL)
	{
		goto fail_map_node_coords11;
	}

	self->map_node_coords13 = cc_map_new();
	if(self->map_node_coords13 == NULL)
	{
		goto fail_map_node_coords13;
	}

	self->map_node_coords15 = cc_map_new();
	if(self->map_node_coords15 == NULL)
	{
		goto fail_map_node_coords15;
	}

	self->node_info = (osmdb_nodeInfo_t*)
	                  CALLOC(1, sizeof(osmdb_nodeInfo_t) +
	                         256*sizeof(char));
	if(self->node_info == NULL)
	{
		goto fail_node_info;
	}

	self->seg_nds = (osmdb_wayNds_t*)
	                CALLOC(1, sizeof(osmdb_wayNds_t) +
	                       KML_PARSER_WAY_NDS*sizeof(int64_t));
	if(self->seg_nds == NULL)
	{
		goto fail_seg_nds;
	}

	if(bfs_util_initialize() == 0)
	{
		goto fail_init;
	}

	self->index = osmdb_index_new(db_name,
	                              OSMDB_INDEX_MODE_APPEND,
	                              1, 1.0f);
	if(self->index == NULL)
	{
		goto fail_index;
	}

	self->style = osmdb_style_newFile(style);
	if(self->style == NULL)
	{
		goto fail_style;
	}

	// success
	return self;

	// failure
	fail_style:
		osmdb_index_delete(&self->index);
	fail_index:
		bfs_util_shutdown();
	fail_init:
		FREE(self->seg_nds);
	fail_seg_nds:
		FREE(self->node_info);
	fail_node_info:
		cc_map_delete(&self->map_node_coords15);
	fail_map_node_coords15:
		cc_map_delete(&self->map_node_coords13);
	fail_map_node_coords13:
		cc_map_delete(&self->map_node_coords11);
	fail_map_node_coords11:
		cc_map_delete(&self->map_node_coords9);
	fail_map_node_coords9:
		cc_map_delete(&self->map_node_coords7);
	fail_map_node_coords7:
		cc_map_delete(&self->map_node_coords5);
	fail_map_node_coords5:
		cc_map_delete(&self->map_node_coords3);
	fail_map_node_coords3:
		cc_list_delete(&self->list_state);
	fail_list_state:
		FREE(self);
	return 0;
}

void kml_parser_delete(kml_parser_t** _self)
{
	ASSERT(_self);

	kml_parser_t* self = *_self;
	if(self)
	{
		cc_mapIter_t* miter;
		miter = cc_map_head(self->map_node_coords3);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords3,
			                           &miter);
			FREE(node_coord);
		}

		miter = cc_map_head(self->map_node_coords5);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords5,
			                           &miter);
			FREE(node_coord);
		}

		miter = cc_map_head(self->map_node_coords7);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords7,
			                           &miter);
			FREE(node_coord);
		}

		miter = cc_map_head(self->map_node_coords9);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords9,
			                           &miter);
			FREE(node_coord);
		}

		miter = cc_map_head(self->map_node_coords11);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords11,
			                           &miter);
			FREE(node_coord);
		}

		miter = cc_map_head(self->map_node_coords13);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords13,
			                           &miter);
			FREE(node_coord);
		}

		miter = cc_map_head(self->map_node_coords15);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*)
			             cc_map_remove(self->map_node_coords15,
			                           &miter);
			FREE(node_coord);
		}

		cc_listIter_t* iter = cc_list_head(self->list_state);
		while(iter)
		{
			int* _state = (int*)
			              cc_list_remove(self->list_state, &iter);
			FREE(_state);
		}

		osmdb_style_delete(&self->style);
		osmdb_index_delete(&self->index);
		bfs_util_shutdown();
		FREE(self->seg_nds);
		FREE(self->node_info);
		cc_map_delete(&self->map_node_coords15);
		cc_map_delete(&self->map_node_coords13);
		cc_map_delete(&self->map_node_coords11);
		cc_map_delete(&self->map_node_coords9);
		cc_map_delete(&self->map_node_coords7);
		cc_map_delete(&self->map_node_coords5);
		cc_map_delete(&self->map_node_coords3);
		cc_list_delete(&self->list_state);
		FREE(self);
		*_self = NULL;
	}
}

int kml_parser_parse(kml_parser_t* self,
                     const char* fname_kml)
{
	ASSERT(self);

	if(xml_istream_parse((void*) self,
	                     kml_parser_start,
	                     kml_parser_end,
	                     fname_kml) == 0)
	{
		return 0;
	}

	return 1;
}

int kml_parser_finish(kml_parser_t* self)
{
	ASSERT(self);

	cc_map_t* map_node_coords[] =
	{
		self->map_node_coords3,
		self->map_node_coords5,
		self->map_node_coords7,
		self->map_node_coords9,
		self->map_node_coords11,
		self->map_node_coords13,
		self->map_node_coords15,
		NULL
	};

	int min_zoom[] =
	{
		3, 5, 7, 9, 11, 13, 15, 0
	};

	int i = 0;
	while(map_node_coords[i])
	{
		// add node coords
		cc_mapIter_t* miter;
		miter = cc_map_head(map_node_coords[i]);
		while(miter)
		{
			osmdb_nodeCoord_t* node_coord;
			node_coord = (osmdb_nodeCoord_t*) cc_map_val(miter);

			size_t size = osmdb_nodeCoord_sizeof(node_coord);
			if(osmdb_index_add(self->index,
			                   OSMDB_TYPE_NODECOORD,
			                   node_coord->nid, size,
			                   (void*) node_coord) == 0)
			{
				return 0;
			}

			if(kml_parser_addTileCoord(self, node_coord->nid,
			                           node_coord->lat,
			                           node_coord->lon,
			                           min_zoom[i]) == 0)
			{
				return 0;
			}

			miter = cc_map_next(miter);
		}

		++i;
	}

	return 1;
}

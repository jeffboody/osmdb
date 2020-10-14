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

#define LOG_TAG "kml2data"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "osmdb/osmdb_util.h"

#define KML_STATE_INIT             0
#define KML_STATE_KML              1
#define KML_STATE_DOCUMENT         2
#define KML_STATE_FOLDER           3
#define KML_STATE_PLACEMARK        4
#define KML_STATE_PLACEMARKNAME    5
#define KML_STATE_POLYGON          6
#define KML_STATE_MULTIGEOMETRY    7
#define KML_STATE_OUTERBOUNDARYIS  8
#define KML_STATE_INNERBOUNDARYIS  9
#define KML_STATE_LINEARRING       10
#define KML_STATE_COORDINATES      11
#define KML_STATE_EXTENDEDDATA     12
#define KML_STATE_SCHEMADATA       13
#define KML_STATE_SIMPLEDATA       14

#define KML_SIMPLEDATA_UNKNOWN 0
#define KML_SIMPLEDATA_TYPE    1

#define KML_PARSER_WAY_NDS 64

/***********************************************************
* private - kml_node_t                                     *
***********************************************************/

static kml_node_t*
kml_node_new(double id, double lat, double lon)
{
	kml_node_t* self;
	self = (kml_node_t*)
	       MALLOC(sizeof(kml_node_t));
	if(self == NULL)
	{
		LOGE("MALLOC failed");
		return NULL;
	}

	self->id  = id;
	self->lat = lat;
	self->lon = lon;

	return self;
}

static void
kml_node_delete(kml_node_t** _self)
{
	ASSERT(_self);

	kml_node_t* self = *_self;
	if(self)
	{
		FREE(self);
		*_self = NULL;
	}
}

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

static void
kml_parser_wayAddSeg(kml_parser_t* self)
{
	ASSERT(self);

	// see init.sql for table definition
	if(self->seg_nds)
	{
		fprintf(self->tbl_ways, "%0.0lf|%i|0|||0|0|0|0|1|11\n",
		        self->wid, self->class);
		fprintf(self->tbl_ways_range, "%0.0lf|%lf|%lf|%lf|%lf\n",
		        self->wid, self->seg_lonL, self->seg_lonR,
		        self->seg_latB, self->seg_latT);
	}
	self->wid     -= 1.0;
	self->seg_nds  = 0;
	self->seg_latT = 0.0;
	self->seg_lonL = 0.0;
	self->seg_latB = 0.0;
	self->seg_lonR = 0.0;
}

static void
kml_parser_wayAddNd(kml_parser_t* self, kml_node_t* node)
{
	ASSERT(self);

	// update bounding boxes
	if(self->way_nds)
	{
		if(node->lat > self->way_latT)
		{
			self->way_latT = node->lat;
		}
		if(node->lon < self->way_lonL)
		{
			self->way_lonL = node->lon;
		}
		if(node->lat < self->way_latB)
		{
			self->way_latB = node->lat;
		}
		if(node->lon > self->way_lonR)
		{
			self->way_lonR = node->lon;
		}
	}
	else
	{
		self->way_latT = node->lat;
		self->way_lonL = node->lon;
		self->way_latB = node->lat;
		self->way_lonR = node->lon;
	}

	if(self->seg_nds)
	{
		if(node->lat > self->seg_latT)
		{
			self->seg_latT = node->lat;
		}
		if(node->lon < self->seg_lonL)
		{
			self->seg_lonL = node->lon;
		}
		if(node->lat < self->seg_latB)
		{
			self->seg_latB = node->lat;
		}
		if(node->lon > self->seg_lonR)
		{
			self->seg_lonR = node->lon;
		}
	}
	else
	{
		self->seg_latT = node->lat;
		self->seg_lonL = node->lon;
		self->seg_latB = node->lat;
		self->seg_lonR = node->lon;
	}

	// see init.sql for table definition
	fprintf(self->tbl_ways_nds, "%i|%0.0lf|%0.0lf\n",
	        self->seg_nds, self->wid, node->id);
	++self->way_nds;
	++self->seg_nds;
}

static int
kml_parser_parseNode(kml_parser_t* self, char* s)
{
	ASSERT(self);
	ASSERT(s);

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

	kml_node_t* node;
	cc_mapIter_t miterator;
	node = (kml_node_t*)
	       cc_map_findf(self->map_nodes, &miterator,
	                    "%lf,%lf", lat, lon);
	if(node == NULL)
	{
		node = kml_node_new(self->nid, lat, lon);
		if(node == NULL)
		{
			return 0;
		}

		if(cc_map_addf(self->map_nodes, (const void*) node,
		               "%lf,%lf", lat, lon) == 0)
		{
			kml_node_delete(&node);
			return 0;
		}

		// advance the next node id
		self->nid -= 1.0;
	}

	kml_parser_wayAddNd(self, node);

	// split way to avoid very large ways
	if(self->seg_nds >= KML_PARSER_WAY_NDS)
	{
		kml_parser_wayAddSeg(self);
		kml_parser_wayAddNd(self, node);
	}

	return 1;
}

static int
kml_parser_parseContent(kml_parser_t* self, char* content)
{
	ASSERT(self);
	ASSERT(content);

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

			if(kml_parser_parseNode(self, s) == 0)
			{
				return 0;
			}

			++idx;
			s = &content[idx];
			continue;
		}

		++idx;
	}

	return kml_parser_parseNode(self, s);
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
kml_parser_endPlacemark(kml_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

	if(self->way_nds && self->class && (self->name[0] != '\0'))
	{
		// see init.sql for table definition
		double lat = self->way_latB +
		             (self->way_latT - self->way_latB)/2.0;
		double lon = self->way_lonL +
		             (self->way_lonR - self->way_lonL)/2.0;

		fprintf(self->tbl_nodes_coords, "%0.0lf|%lf|%lf\n",
		        self->nid, lat, lon);
		fprintf(self->tbl_nodes_info, "%0.0lf|%i|%s||0|0|11\n",
		        self->nid, self->class, self->name);

		// advance the next node id
		self->nid -= 1.0;
	}

	snprintf(self->name, 256, "%s", "");
	self->class = 0;

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

	kml_parser_wayAddSeg(self);

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
		if(self->simpledata == KML_SIMPLEDATA_TYPE)
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
                            int line,
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
                          int line,
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

kml_parser_t* kml_parser_new(void)
{
	kml_parser_t* self = (kml_parser_t*)
	                     CALLOC(1, sizeof(kml_parser_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	// -1.0 is reserved for an invalid ID
	self->nid   = -2.0;
	self->wid   = -2.0;
	self->class = osmdb_classNameToCode("class:none");

	snprintf(self->name, 256, "%s", "");

	self->list_state = cc_list_new();
	if(self->list_state == NULL)
	{
		goto fail_list_state;
	}

	self->map_nodes = cc_map_new();
	if(self->map_nodes == NULL)
	{
		goto fail_map_nodes;
	}

	self->tbl_nodes_coords = fopen("tbl_nodes_coords.data", "w");
	if(self->tbl_nodes_coords == NULL)
	{
		LOGE("fopen failed");
		goto fail_tbl_nodes_coords;
	}

	self->tbl_nodes_info = fopen("tbl_nodes_info.data", "w");
	if(self->tbl_nodes_info == NULL)
	{
		LOGE("fopen failed");
		goto fail_tbl_nodes_info;
	}

	self->tbl_ways = fopen("tbl_ways.data", "w");
	if(self->tbl_ways == NULL)
	{
		LOGE("fopen failed");
		goto fail_tbl_ways;
	}

	self->tbl_ways_range = fopen("tbl_ways_range.data", "w");
	if(self->tbl_ways_range == NULL)
	{
		LOGE("fopen failed");
		goto fail_tbl_ways_range;
	}

	self->tbl_ways_nds = fopen("tbl_ways_nds.data", "w");
	if(self->tbl_ways_nds == NULL)
	{
		LOGE("fopen failed");
		goto fail_tbl_ways_nds;
	}

	// success
	return self;

	// failure
	fail_tbl_ways_nds:
		fclose(self->tbl_ways_range);
	fail_tbl_ways_range:
		fclose(self->tbl_ways);
	fail_tbl_ways:
		fclose(self->tbl_nodes_info);
	fail_tbl_nodes_info:
		fclose(self->tbl_nodes_coords);
	fail_tbl_nodes_coords:
		cc_map_delete(&self->map_nodes);
	fail_map_nodes:
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
		fclose(self->tbl_ways_nds);
		fclose(self->tbl_ways_range);
		fclose(self->tbl_ways);
		fclose(self->tbl_nodes_info);
		fclose(self->tbl_nodes_coords);

		cc_mapIter_t  miterator;
		cc_mapIter_t* miter;
		miter = cc_map_head(self->map_nodes, &miterator);
		while(miter)
		{
			kml_node_t* node;
			node = (kml_node_t*)
			       cc_map_remove(self->map_nodes, &miter);
			kml_node_delete(&node);
		}

		cc_listIter_t* iter = cc_list_head(self->list_state);
		while(iter)
		{
			int* _state = (int*)
			              cc_list_remove(self->list_state, &iter);
			FREE(_state);
		}
		cc_map_delete(&self->map_nodes);
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

void kml_parser_finish(kml_parser_t* self)
{
	ASSERT(self);

	// add node coords
	cc_mapIter_t  miterator;
	cc_mapIter_t* miter;
	miter = cc_map_head(self->map_nodes, &miterator);
	while(miter)
	{
		kml_node_t* node = (kml_node_t*) cc_map_val(miter);

		// see init.sql for table definition
		fprintf(self->tbl_nodes_coords, "%0.0lf|%lf|%lf\n",
		        node->id, node->lat, node->lon);
		miter = cc_map_next(miter);
	}
}

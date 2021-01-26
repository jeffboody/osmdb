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

#include <iconv.h>
#include <inttypes.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_timestamp.h"
#include "libxmlstream/xml_istream.h"
#include "osmdb/osmdb_util.h"
#include "terrain/terrain_util.h"
#include "osm_parser.h"

// protected functions
int osmdb_index_updateChangeset(osmdb_index_t* self,
                                int64_t changeset);
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

#define OSM_STATE_INIT            0
#define OSM_STATE_OSM             1
#define OSM_STATE_OSM_BOUNDS      2
#define OSM_STATE_OSM_NODE        3
#define OSM_STATE_OSM_NODE_TAG    4
#define OSM_STATE_OSM_WAY         5
#define OSM_STATE_OSM_WAY_TAG     6
#define OSM_STATE_OSM_WAY_ND      7
#define OSM_STATE_OSM_REL         8
#define OSM_STATE_OSM_REL_TAG     9
#define OSM_STATE_OSM_REL_MEMBER 10
#define OSM_STATE_DONE           -1

#define ICONV_OPEN_ERR ((iconv_t) (-1))
#define ICONV_CONV_ERR ((size_t) (-1))

/***********************************************************
* private - class utils                                    *
***********************************************************/

static void osm_parser_discardClass(osm_parser_t* self)
{
	ASSERT(self);

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(self->class_map, &iterator);
	while(iter)
	{
		int* cls;
		cls = (int*) cc_map_remove(self->class_map, &iter);
		FREE(cls);
	}
}

static int osm_parser_findClass(osm_parser_t* self,
                                const char* key,
                                const char* val)
{
	ASSERT(self);
	ASSERT(key);
	ASSERT(val);

	cc_mapIter_t iter;
	int* cls = (int*) cc_map_findf(self->class_map, &iter,
	                               "%s:%s", key, val);

	return cls ? *cls : 0;
}

static int
osm_parser_fillClass(osm_parser_t* self)
{
	ASSERT(self);

	int  i;
	int* cls;
	int  class_count = osmdb_classCount();
	for(i = 0; i < class_count; ++i)
	{
		cls = (int*) MALLOC(sizeof(int));
		if(cls == NULL)
		{
			goto fail_malloc;
		}
		*cls = i;

		if(cc_map_add(self->class_map,
		              (const void*) cls,
		              osmdb_classCodeToName(i)) == 0)
		{
			goto fail_add;
		}
	}

	// success
	return 1;

	// failure
	fail_add:
		FREE(cls);
	fail_malloc:
		osm_parser_discardClass(self);
	return 0;
}

/***********************************************************
* private - parsing utils                                  *
***********************************************************/

typedef struct
{
	int abreviate;
	char word[256];
	char abrev[256];
	char sep[2];
} osm_token_t;

static int osm_abreviateWord(const char* a, char* b)
{
	ASSERT(a);
	ASSERT(b);

	int abreviate = 1;

	// abreviations based loosely on
	// https://github.com/nvkelso/map-label-style-manual
	// http://pe.usps.gov/text/pub28/28c1_001.htm
	if(strncmp(a, "North", 256) == 0)
	{
		strncat(b, "N", 256);
	}
	else if(strncmp(a, "East", 256) == 0)
	{
		strncat(b, "E", 256);
	}
	else if(strncmp(a, "South", 256) == 0)
	{
		strncat(b, "S", 256);
	}
	else if(strncmp(a, "West", 256) == 0)
	{
		strncat(b, "W", 256);
	}
	else if(strncmp(a, "Northeast", 256) == 0)
	{
		strncat(b, "NE", 256);
	}
	else if(strncmp(a, "Northwest", 256) == 0)
	{
		strncat(b, "NW", 256);
	}
	else if(strncmp(a, "Southeast", 256) == 0)
	{
		strncat(b, "SE", 256);
	}
	else if(strncmp(a, "Southwest", 256) == 0)
	{
		strncat(b, "SW", 256);
	}
	else if(strncmp(a, "Avenue", 256) == 0)
	{
		strncat(b, "Ave", 256);
	}
	else if(strncmp(a, "Boulevard", 256) == 0)
	{
		strncat(b, "Blvd", 256);
	}
	else if(strncmp(a, "Court", 256) == 0)
	{
		strncat(b, "Ct", 256);
	}
	else if(strncmp(a, "Circle", 256) == 0)
	{
		strncat(b, "Cir", 256);
	}
	else if(strncmp(a, "Drive", 256) == 0)
	{
		strncat(b, "Dr", 256);
	}
	else if(strncmp(a, "Expressway", 256) == 0)
	{
		strncat(b, "Expwy", 256);
	}
	else if(strncmp(a, "Freeway", 256) == 0)
	{
		strncat(b, "Fwy", 256);
	}
	else if(strncmp(a, "Highway", 256) == 0)
	{
		strncat(b, "Hwy", 256);
	}
	else if(strncmp(a, "Lane", 256) == 0)
	{
		strncat(b, "Ln", 256);
	}
	else if(strncmp(a, "Parkway", 256) == 0)
	{
		strncat(b, "Pkwy", 256);
	}
	else if(strncmp(a, "Place", 256) == 0)
	{
		strncat(b, "Pl", 256);
	}
	else if(strncmp(a, "Road", 256) == 0)
	{
		strncat(b, "Rd", 256);
	}
	else if(strncmp(a, "Street", 256) == 0)
	{
		strncat(b, "St", 256);
	}
	else if(strncmp(a, "Terrace", 256) == 0)
	{
		strncat(b, "Ter", 256);
	}
	else if(strncmp(a, "Trail", 256) == 0)
	{
		strncat(b, "Tr", 256);
	}
	else if((strncmp(a, "Mount", 256) == 0) ||
	        (strncmp(a, "Mt.",   256) == 0))
	{
		strncat(b, "Mt", 256);
	}
	else if(strncmp(a, "Mountain", 256) == 0)
	{
		strncat(b, "Mtn", 256);
	}
	else
	{
		strncat(b, a, 256);
		abreviate = 0;
	}
	b[255] = '\0';

	return abreviate;
}

static void osm_catWord(char* str, char* word)
{
	ASSERT(str);
	ASSERT(word);

	strncat(str, word, 256);
	str[255] = '\0';
}

static const char* osm_parseWord(int line,
                                 const char* str,
                                 osm_token_t* tok)
{
	ASSERT(str);
	ASSERT(tok);

	tok->abreviate = 0;
	tok->word[0]   = '\0';
	tok->abrev[0]  = '\0';
	tok->sep[0]    = '\0';
	tok->sep[1]    = '\0';

	// eat whitespace
	int i = 0;
	while(1)
	{
		char c = str[i];
		if((c == ' ')  ||
		   (c == '\n') ||
		   (c == '\t') ||
		   (c == '\r'))
		{
			++i;
			continue;
		}

		break;
	}

	// find a word
	int len = 0;
	while(1)
	{
		char c = str[i];

		// validate characters
		// disallow '"' because of "Skyscraper Peak", etc.
		// disallow '|' since it is used as a SQL data separator
		if((c == '\n') ||
		   (c == '\t') ||
		   (c == '\r') ||
		   (c == '"'))
		{
			// eat unsupported characters
			// if(c == '"')
			// {
			// 	LOGW("quote i=%i, line=%i, str=%s",
			// 	     i, line, str);
			// }
			++i;
			continue;
		}
		else if(c == '|')
		{
			// pipe is reserved for SQLite tables
			c = ' ';
		}
		else if(((c >= 32) && (c <= 126)) ||
		        (c == '\0'))
		{
			// accept printable characters and null char
		}
		else
		{
			// eat invalid characters
			// LOGW("invalid line=%i, c=0x%X, str=%s",
			//      line, (unsigned int) c, str);
			++i;
			continue;
		}

		// check for word boundary
		if((c == '\0') && (len == 0))
		{
			return NULL;
		}
		else if(len == 255)
		{
			// LOGW("invalid line=%i",line);
			return NULL;
		}
		else if(c == '\0')
		{
			tok->abreviate = osm_abreviateWord(tok->word,
			                                   tok->abrev);
			return &str[i];
		}
		else if(c == ' ')
		{
			tok->abreviate = osm_abreviateWord(tok->word,
			                                   tok->abrev);
			tok->sep[0] = ' ';
			return &str[i + 1];
		}
		else if(c == ';')
		{
			tok->abreviate = osm_abreviateWord(tok->word,
			                                   tok->abrev);
			tok->sep[0] = ';';
			return &str[i + 1];
		}

		// append character to word
		tok->word[len]     = c;
		tok->word[len + 1] = '\0';
		++len;
		++i;
	}
}

static int
osm_parseName(int line, const char* input, char* name,
              char* abrev)
{
	ASSERT(input);
	ASSERT(name);
	ASSERT(abrev);

	// initialize output string
	name[0]  = '\0';
	abrev[0] = '\0';

	// parse all words
	const char* str   = input;
	const int   WORDS = 16;
	int         words = 0;
	osm_token_t word[WORDS];
	while(str && (words < WORDS))
	{
		str = osm_parseWord(line, str, &word[words]);
		if(str)
		{
			++words;
		}
	}

	// trim elevation from name
	// e.g. "Mt Meeker 13,870 ft"
	if((words >= 2) &&
	   (strncmp(word[words - 1].word, "ft", 256) == 0))
	{
		// LOGW("trim %s", input);
		words -= 2;
	}

	if(words == 0)
	{
		// input is null string
		// LOGW("invalid line=%i, name=%s", line, input);
		return 0;
	}
	else if(words == 1)
	{
		// input is single word (don't abreviate)
		snprintf(name, 256, "%s", word[0].word);
		return 1;
	}
	else if(words == 2)
	{
		osm_catWord(name, word[0].word);
		osm_catWord(name, word[0].sep);
		osm_catWord(name, word[1].word);

		// input is two words
		if(word[1].abreviate)
		{
			// don't abreviate first word if second
			// word is also abrev
			osm_catWord(abrev, word[0].word);
			osm_catWord(abrev, word[0].sep);
			osm_catWord(abrev, word[1].abrev);
		}
		else if(word[0].abreviate)
		{
			osm_catWord(abrev, word[0].abrev);
			osm_catWord(abrev, word[0].sep);
			osm_catWord(abrev, word[1].word);
		}
		return 1;
	}

	// three or more words
	// end of special cases
	int abreviate = 0;
	osm_catWord(name, word[0].word);
	osm_catWord(name, word[0].sep);
	if(word[0].abreviate)
	{
		abreviate = 1;
		osm_catWord(abrev, word[0].abrev);
	}
	else
	{
		osm_catWord(abrev, word[0].word);
	}
	osm_catWord(abrev, word[0].sep);

	osm_catWord(name, word[1].word);
	if(word[1].abreviate)
	{
		abreviate = 1;
		osm_catWord(abrev, word[1].abrev);
	}
	else
	{
		osm_catWord(abrev, word[1].word);
	}

	// parse the rest of the line
	int n = 2;
	while(n < words)
	{
		osm_catWord(name, word[n - 1].sep);
		osm_catWord(name, word[n].word);

		osm_catWord(abrev, word[n - 1].sep);

		if(word[n].abreviate)
		{
			abreviate = 1;
			osm_catWord(abrev, word[n].abrev);
		}
		else
		{
			osm_catWord(abrev, word[n].word);
		}

		++n;
	}

	// clear abrev when no words abreviated
	if(abreviate == 0)
	{
		abrev[0] = '\0';
	}

	return 1;
}

static int osm_parseEle(int line, const char* a, int ft)
{
	ASSERT(a);

	// assume the ele is in meters
	float ele = strtof(a, NULL);
	if(ft == 0)
	{
		// convert meters to ft
		ele *= 3937.0f/1200.0f;
	}

	osm_token_t w0;
	osm_token_t w1;
	osm_token_t wn;

	const char* str = a;
	str = osm_parseWord(line, str, &w0);
	if(str == NULL)
	{
		// input is null string
		// LOGW("invalid line=%i, ele=%s", line, a);
		return 0;
	}

	str = osm_parseWord(line, str, &w1);
	if(str == NULL)
	{
		// input is single word
		return (int) (ele + 0.5f);
	}

	str = osm_parseWord(line, str, &wn);
	if(str == NULL)
	{
		// check if w1 is ft
		if((strcmp(w1.word, "ft")   == 0) ||
		   (strcmp(w1.word, "feet") == 0))
		{
			// assume w0 is in ft
			float ele = strtof(w0.word, NULL);
			return (int) (ele + 0.5f);
		}
		else
		{
			// LOGW("invalid line=%i, ele=%s", line, a);
			return 0;
		}
	}

	// LOGW("invalid line=%i, ele=%s", line, a);
	return 0;
}

static void
osm_parser_iconv(osm_parser_t* self,
                 const char* input,
                 char* output)
{
	ASSERT(self);
	ASSERT(input);
	ASSERT(output);

	// https://rt.cpan.org/Public/Bug/Display.html?id=103901
	char buf[256];
	snprintf(buf, 256, "%s", input);

	char*  iptr  = &(buf[0]);
	char*  optr  = &(output[0]);
	size_t isize = strlen(iptr) + 1;
	size_t osize = 256;

	int step = 0;
	while(step < 16)
	{
		int ret = iconv(self->cd, &iptr, &isize, &optr, &osize);
		if(ret == ICONV_CONV_ERR)
		{
			break;
		}
		else if(isize == 0)
		{
			// reset the iconv state
			iconv(self->cd, NULL, NULL, NULL, NULL);

			// success
			return;
		}

		// some encodings may require multiple steps to convert
		++step;
	}

	// reset the iconv state
	iconv(self->cd, NULL, NULL, NULL, NULL);

	// rely on parseWord to discard unicode characters
	snprintf(output, 256, "%s", input);
}

/***********************************************************
* private                                                  *
***********************************************************/

static int
osm_parser_logProgress(osm_parser_t* self, double* _dt)
{
	ASSERT(self);
	ASSERT(dt);

	double t2 = cc_timestamp();
	double dt = t2 - self->t1;
	*_dt = t2 - self->t0;
	if(dt >= 10.0)
	{
		self->t1 = t2;
		return 1;
	}

	return 0;
}

static void osm_parser_initNode(osm_parser_t* self)
{
	ASSERT(self);

	memset((void*) self->node_coord, 0,
	       sizeof(osmdb_nodeCoord_t));
	memset((void*) self->node_info, 0,
	       sizeof(osmdb_nodeInfo_t));

	self->node_coord->nid = -1;
	self->node_info->nid  = -1;

	self->name_en      = 0;
	self->tag_name[0]  = '\0';
	self->tag_abrev[0] = '\0';
}

static void osm_parser_initWay(osm_parser_t* self)
{
	ASSERT(self);

	memset((void*) self->way_info, 0,
	       sizeof(osmdb_wayInfo_t));
	memset((void*) self->way_range, 0,
	       sizeof(osmdb_wayRange_t));
	memset((void*) self->way_nds, 0,
	       sizeof(osmdb_wayNds_t));

	self->way_info->wid  = -1;
	self->way_range->wid = -1;
	self->way_nds->wid   = -1;

	self->name_en      = 0;
	self->tag_name[0]  = '\0';
	self->tag_abrev[0] = '\0';
}

static void osm_parser_initRel(osm_parser_t* self)
{
	ASSERT(self);

	memset((void*) self->rel_info, 0,
	       sizeof(osmdb_relInfo_t));
	memset((void*) self->rel_range, 0,
	       sizeof(osmdb_relRange_t));
	memset((void*) self->rel_members, 0,
	       sizeof(osmdb_relMembers_t));

	self->rel_info->rid    = -1;
	self->rel_info->nid    = -1;
	self->rel_range->rid   = -1;
	self->rel_members->rid = -1;

	self->name_en      = 0;
	self->tag_name[0]  = '\0';
	self->tag_abrev[0] = '\0';
}

static int
osm_parser_beginOsm(osm_parser_t* self, int line,
                    const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM;

	return 1;
}

static int
osm_parser_endOsm(osm_parser_t* self, int line,
                  const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_DONE;

	return osmdb_index_updateChangeset(self->index,
	                                   self->tag_changeset);
}

static int
osm_parser_beginOsmBounds(osm_parser_t* self, int line,
                          const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_BOUNDS;

	return 1;
}

static int
osm_parser_endOsmBounds(osm_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	return 1;
}

static int
osm_parser_beginOsmNode(osm_parser_t* self, int line,
                        const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_NODE;
	osm_parser_initNode(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->node_coord->nid = (int64_t)
			                        strtoll(atts[j], NULL, 0);
			self->node_info->nid  = self->node_coord->nid;
		}
		else if(strcmp(atts[i], "changeset") == 0)
		{
			int64_t changeset = (int64_t)
			                    strtoll(atts[j], NULL, 0);
			if(changeset > self->tag_changeset)
			{
				self->tag_changeset = changeset;
			}
		}
		else if(strcmp(atts[i], "lat") == 0)
		{
			self->node_coord->lat = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "lon") == 0)
		{
			self->node_coord->lon = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_addTileCoord(osm_parser_t* self,
                        int64_t ref,
                        double lat, double lon,
                        int min_zoom)
{
	ASSERT(self);

	float   x;
	float   y;
	int64_t id;

	int ix;
	int iy;
	int i       = 0;
	int zoom[]  = { 14, 11, -1 };
	int pow2n[] = { 16384, 2048 };

	int type_array[]  =
	{
		OSMDB_TYPE_TILEREF_NODE14,
		OSMDB_TYPE_TILEREF_NODE11,
	};

	while(min_zoom <= zoom[i])
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
osm_parser_insertNodeInfo(osm_parser_t* self, int min_zoom)
{
	ASSERT(self);

	size_t size;
	size = osmdb_nodeInfo_sizeof(self->node_info);
	if(osmdb_index_add(self->index,
	                   OSMDB_TYPE_NODEINFO,
	                   self->node_info->nid,
	                   size, (void*) self->node_info) == 0)
	{
		return 0;
	}

	if(osm_parser_addTileCoord(self,
	                           self->node_coord->nid,
	                           self->node_coord->lat,
	                           self->node_coord->lon,
	                           min_zoom) == 0)
	{
		return 0;
	}

	return 1;
}

static int
osm_parser_insertNodeCoords(osm_parser_t* self)
{
	ASSERT(self);

	size_t size;
	size = osmdb_nodeCoord_sizeof(self->node_coord);
	return osmdb_index_add(self->index,
	                       OSMDB_TYPE_NODECOORD,
	                       self->node_coord->nid,
	                       size, (void*) self->node_coord);
}

static int
osm_parser_endOsmNode(osm_parser_t* self, int line,
                      float progress, const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	// select nodes when a point and name exists
	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style,
	                       osmdb_classCodeToName(self->node_info->class));
	if(sc && sc->point && (self->tag_name[0] != '\0'))
	{
		int min_zoom = sc->point->min_zoom;

		// fill the name
		if(self->tag_abrev[0] == '\0')
		{
			osmdb_nodeInfo_addName(self->node_info,
			                       self->tag_name);
		}
		else
		{
			osmdb_nodeInfo_addName(self->node_info,
			                       self->tag_abrev);
		}

		if(osm_parser_insertNodeInfo(self, min_zoom) == 0)
		{
			return 0;
		}
	}

	// node coords may be transitively selected
	if(osm_parser_insertNodeCoords(self) == 0)
	{
		return 0;
	}

	++self->count_nodes;

	double dt;
	if(osm_parser_logProgress(self, &dt))
	{
		LOGI("dt=%0.0lf, progress=%f, memsize=%" PRId64 ", count=%" PRIu64,
		     dt, 100.0f*progress, (int64_t) MEMSIZE(), self->count_nodes);
	}

	return 1;
}

static int
osm_parser_beginOsmNodeTag(osm_parser_t* self, int line,
                           const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_NODE_TAG;

	int  i = 0;
	int  j = 1;
	int  m = 2;
	int  n = 3;
	char val[256];
	while(atts[i] && atts[j] && atts[m] && atts[n])
	{
		if((strcmp(atts[i], "k") == 0) &&
		   (strcmp(atts[m], "v") == 0))
		{
			// iconv value
			osm_parser_iconv(self, atts[n], val);

			char name[256];
			char abrev[256];

			int class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				// set or overwrite generic class
				if((self->node_info->class == self->class_none)   ||
				   (self->node_info->class == self->building_yes) ||
				   (self->node_info->class == self->barrier_yes)  ||
				   (self->node_info->class == self->office_yes)   ||
				   (self->node_info->class == self->historic_yes) ||
				   (self->node_info->class == self->man_made_yes) ||
				   (self->node_info->class == self->tourism_yes))
				{
					self->node_info->class = class;
				}
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        (self->name_en == 0) &&
			        osm_parseName(line, val, name, abrev))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if((strcmp(atts[j], "name:en") == 0) &&
			        osm_parseName(line, val, name, abrev))
			{
				self->name_en = 1;
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if(strcmp(atts[j], "ele:ft") == 0)
			{
				self->node_info->ele = osm_parseEle(line, val, 1);
			}
			else if(strcmp(atts[j], "ele") == 0)
			{
				self->node_info->ele = osm_parseEle(line, val, 0);
			}
		}

		i += 4;
		j += 4;
		m += 4;
		n += 4;
	}

	return 1;
}

static int
osm_parser_endOsmNodeTag(osm_parser_t* self, int line,
                         const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM_NODE;

	return 1;
}

static int
osm_parser_beginOsmWay(osm_parser_t* self, int line,
                       const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_WAY;
	osm_parser_initWay(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->way_info->wid  = (int64_t)
			                       strtoll(atts[j], NULL, 0);
			self->way_range->wid = self->way_info->wid;
			self->way_nds->wid   = self->way_info->wid;
		}
		else if(strcmp(atts[i], "changeset") == 0)
		{
			int64_t changeset = (int64_t)
			                    strtoll(atts[j], NULL, 0);
			if(changeset > self->tag_changeset)
			{
				self->tag_changeset = changeset;
			}
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_computeWayRange(osm_parser_t* self,
                           osmdb_wayNds_t*   way_nds,
                           osmdb_wayRange_t* way_range)
{
	ASSERT(self);
	ASSERT(way_nds);
	ASSERT(way_range);

	osmdb_handle_t*    hnd_node_coord;
	osmdb_nodeCoord_t* node_coord;

	// ignore
	if(way_nds->count == 0)
	{
		return 1;
	}

	int64_t* nds = osmdb_wayNds_nds(way_nds);

	int i;
	int first = 1;
	for(i = 0; i < way_nds->count; ++i)
	{
		if(osmdb_index_get(self->index, 0,
		                   OSMDB_TYPE_NODECOORD,
		                   nds[i], &hnd_node_coord) == 0)
		{
			return 0;
		}

		// some ways may not exist due to osmosis
		if(hnd_node_coord == NULL)
		{
			continue;
		}
		node_coord = hnd_node_coord->node_coord;

		if(first)
		{
			way_range->latT = node_coord->lat;
			way_range->lonL = node_coord->lon;
			way_range->latB = node_coord->lat;
			way_range->lonR = node_coord->lon;

			first = 0;
		}
		else
		{
			if(node_coord->lat > way_range->latT)
			{
				way_range->latT = node_coord->lat;
			}

			if(node_coord->lon < way_range->lonL)
			{
				way_range->lonL = node_coord->lon;
			}

			if(node_coord->lat < way_range->latB)
			{
				way_range->latB = node_coord->lat;
			}

			if(node_coord->lon > way_range->lonR)
			{
				way_range->lonR = node_coord->lon;
			}
		}

		osmdb_index_put(self->index, &hnd_node_coord);
	}

	return 1;
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
osm_parser_addTileRange(osm_parser_t* self,
                        int type, int64_t ref,
                        double latT, double lonL,
                        double latB, double lonR,
                        int center, int polygon,
                        int min_zoom)
{
	ASSERT(self);

	// elements are defined with zero width but in
	// practice are drawn with non-zero width
	// points/lines so a border is needed to ensure they
	// are not clipped between neighboring tiles
	float border = 1.0f/16.0f;

	// center the range
	if(center)
	{
		latT = latB + (latT - latB)/2.0;
		lonR = lonL + (lonR - lonL)/2.0;
		latB = latT;
		lonL = lonR;

		border = 0.0f;
	}

	// determine the tile type
	int type_way[]  =
	{
		OSMDB_TYPE_TILEREF_WAY14,
		OSMDB_TYPE_TILEREF_WAY11,
	};
	int type_rel[]  =
	{
		OSMDB_TYPE_TILEREF_REL14,
		OSMDB_TYPE_TILEREF_REL11,
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
	int   i       = 0;
	int   zoom[]  = { 14, 11, -1 };
	int   pow2n[] = { 16384, 2048 };
	while(min_zoom <= zoom[i])
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
osm_parser_insertWay(osm_parser_t* self,
                     int center, int polygon,
                     int selected, int min_zoom)
{
	ASSERT(self);

	size_t size;
	size = osmdb_wayInfo_sizeof(self->way_info);
	if(osmdb_index_add(self->index,
	                   OSMDB_TYPE_WAYINFO,
	                   self->way_info->wid,
	                   size, (void*) self->way_info) == 0)
	{
		return 0;
	}

	// only compute the range if way was selected
	// or recursively selected by osm_parser_computeRelRange
	if(selected)
	{
		if(osm_parser_computeWayRange(self, self->way_nds,
		                              self->way_range) == 0)
		{
			return 0;
		}

		size = osmdb_wayRange_sizeof(self->way_range);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_WAYRANGE,
		                   self->way_range->wid,
		                   size, (void*) self->way_range) == 0)
		{
			return 0;
		}

		if(osm_parser_addTileRange(self,
		                           OSMDB_TYPE_WAYRANGE,
		                           self->way_range->wid,
		                           self->way_range->latT,
		                           self->way_range->lonL,
		                           self->way_range->latB,
		                           self->way_range->lonR,
		                           center, polygon,
		                           min_zoom) == 0)
		{
			return 0;
		}
	}

	size = osmdb_wayNds_sizeof(self->way_nds);
	if(osmdb_index_add(self->index,
	                   OSMDB_TYPE_WAYNDS,
	                   self->way_nds->wid,
	                   size, (void*) self->way_nds) == 0)
	{
		return 0;
	}

	return 1;
}

static int
osm_parser_endOsmWay(osm_parser_t* self, int line,
                     float progress, const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	int center   = 0;
	int selected = 0;
	int polygon  = 0;

	// select ways when a line/poly exists or when
	// a point and name exists
	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style,
	                       osmdb_classCodeToName(self->way_info->class));
	if(sc && (sc->line || sc->poly))
	{
		if(sc->poly)
		{
			polygon = 1;
		}

		selected = 1;
	}
	else if(sc && sc->point && (self->tag_name[0] != '\0'))
	{
		selected = 1;
		center   = 1;
	}

	// fill the name
	if(self->tag_abrev[0] == '\0')
	{
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_name);
	}
	else
	{
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_abrev);
	}

	// always add ways since they may be transitively selected
	int min_zoom = sc ? osmdb_styleClass_minZoom(sc) : 999;
	if(osm_parser_insertWay(self, center, polygon,
	                        selected, min_zoom) == 0)
	{
		return 0;
	}

	++self->count_ways;

	double dt;
	if(osm_parser_logProgress(self, &dt))
	{
		LOGI("dt=%0.0lf, progress=%f, memsize=%" PRId64 ", count=%" PRIu64,
		     dt, 100.0f*progress, (int64_t) MEMSIZE(), self->count_ways);
	}

	return 1;
}

static int
osm_parser_beginOsmWayTag(osm_parser_t* self, int line,
                          const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_WAY_TAG;

	int  i = 0;
	int  j = 1;
	int  m = 2;
	int  n = 3;
	char val[256];
	while(atts[i] && atts[j] && atts[m] && atts[n])
	{
		if((strcmp(atts[i], "k") == 0) &&
		   (strcmp(atts[m], "v") == 0))
		{
			// iconv value
			osm_parser_iconv(self, atts[n], val);

			char name[256];
			char abrev[256];

			int class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				// set or overwrite generic class
				if((self->way_info->class == self->class_none)   ||
				   (self->way_info->class == self->building_yes) ||
				   (self->way_info->class == self->barrier_yes)  ||
				   (self->way_info->class == self->office_yes)   ||
				   (self->way_info->class == self->historic_yes) ||
				   (self->way_info->class == self->man_made_yes) ||
				   (self->way_info->class == self->tourism_yes))
				{
					self->way_info->class = class;
				}
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        (self->name_en == 0) &&
			        osm_parseName(line, val, name, abrev))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if((strcmp(atts[j], "name:en") == 0) &&
			        osm_parseName(line, val, name, abrev))
			{
				self->name_en = 1;
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if(strcmp(atts[j], "layer") == 0)
			{
				self->way_info->layer = (int) strtol(val, NULL, 0);
			}
			else if(strcmp(atts[j], "oneway") == 0)
			{
				if(strcmp(val, "yes") == 0)
				{
					self->way_info->flags |= OSMDB_WAYINFO_FLAG_FORWARD;
				}
				else if(strcmp(val, "-1") == 0)
				{
					self->way_info->flags |= OSMDB_WAYINFO_FLAG_REVERSE;
				}
			}
			else if((strcmp(atts[j], "bridge") == 0) &&
				    (strcmp(val, "no") != 0))
			{
				self->way_info->flags |= OSMDB_WAYINFO_FLAG_BRIDGE;
			}
			else if((strcmp(atts[j], "tunnel") == 0) &&
				    (strcmp(val, "no") != 0))
			{
				self->way_info->flags |= OSMDB_WAYINFO_FLAG_TUNNEL;
			}
			else if((strcmp(atts[j], "cutting") == 0) &&
				    (strcmp(val, "no") != 0))
			{
				self->way_info->flags |= OSMDB_WAYINFO_FLAG_CUTTING;
			}
		}

		i += 4;
		j += 4;
		m += 4;
		n += 4;
	}

	return 1;
}

static int
osm_parser_endOsmWayTag(osm_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM_WAY;

	return 1;
}

static int
osm_parser_beginOsmWayNd(osm_parser_t* self, int line,
                         const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_WAY_ND;

	// update nds size
	if(self->way_nds_maxCount <= self->way_nds->count)
	{
		osmdb_wayNds_t tmp_size =
		{
			.count=2*self->way_nds_maxCount
		};

		size_t size = osmdb_wayNds_sizeof(&tmp_size);

		osmdb_wayNds_t* tmp;
		tmp = (osmdb_wayNds_t*)
		      REALLOC((void*) self->way_nds, size);
		if(tmp == NULL)
		{
			LOGE("REALLOC failed");
			return 0;
		}

		self->way_nds           = tmp;
		self->way_nds_maxCount  = tmp_size.count;
	}

	int64_t ref = 0;

	// parse the ref
	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "ref") == 0)
		{
			ref = (int64_t) strtoll(atts[j], NULL, 0);
			break;
		}

		i += 2;
		j += 2;
	}

	if(ref == 0)
	{
		LOGE("invalid ref=0");
		return 0;
	}

	int64_t* nds = osmdb_wayNds_nds(self->way_nds);
	nds[self->way_nds->count] = ref;
	self->way_nds->count += 1;

	return 1;
}

static int
osm_parser_endOsmWayNd(osm_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM_WAY;

	return 1;
}

static int
osm_parser_beginOsmRel(osm_parser_t* self, int line,
                       const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_REL;
	osm_parser_initRel(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->rel_info->rid    = (int64_t)
			                         strtoll(atts[j], NULL, 0);
			self->rel_members->rid = self->rel_info->rid;
			self->rel_range->rid   = self->rel_info->rid;
		}
		else if(strcmp(atts[i], "changeset") == 0)
		{
			int64_t changeset = (int64_t)
			                    strtoll(atts[j], NULL, 0);
			if(changeset > self->tag_changeset)
			{
				self->tag_changeset = changeset;
			}
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_computeRelRange(osm_parser_t* self)
{
	ASSERT(self);

	osmdb_handle_t*     hnd_way_range;
	osmdb_handle_t*     hnd_way_nds;
	osmdb_relMembers_t* rel_members = self->rel_members;
	osmdb_relRange_t*   rel_range   = self->rel_range;
	osmdb_wayRange_t*   way_range;
	osmdb_wayRange_t    tmp_way_range;

	// ignore
	if(rel_members->count == 0)
	{
		return 1;
	}

	osmdb_relData_t* data;
	data = osmdb_relMembers_data(rel_members);

	int i;
	int first = 1;
	for(i = 0; i < rel_members->count; ++i)
	{
		if(osmdb_index_get(self->index, 0,
		                   OSMDB_TYPE_WAYRANGE,
		                   data[i].ref, &hnd_way_range) == 0)
		{
			return 0;
		}

		// some way ranges may not exist due to osmosis or must
		// be computed since they were not selected by
		// osm_parser_insertWay
		if(hnd_way_range)
		{
			way_range = hnd_way_range->way_range;
		}
		else
		{
			way_range = &tmp_way_range;
			way_range->wid  = data[i].ref;
			way_range->latT = 0.0;
			way_range->lonL = 0.0;
			way_range->latB = 0.0;
			way_range->lonR = 0.0;

			if(osmdb_index_get(self->index, 0,
			                   OSMDB_TYPE_WAYNDS,
			                   data[i].ref, &hnd_way_nds) == 0)
			{
				return 0;
			}

			// some ways may not exist due to osmosis
			if(hnd_way_nds == NULL)
			{
				continue;
			}

			if(osm_parser_computeWayRange(self,
			                              hnd_way_nds->way_nds,
			                              way_range) == 0)
			{
				osmdb_index_put(self->index, &hnd_way_nds);
				return 0;
			}

			size_t size;
			size = osmdb_wayRange_sizeof(way_range);
			if(osmdb_index_add(self->index,
			                   OSMDB_TYPE_WAYRANGE,
			                   way_range->wid,
			                   size, (void*) way_range) == 0)
			{
				osmdb_index_put(self->index, &hnd_way_nds);
				return 0;
			}

			osmdb_index_put(self->index, &hnd_way_nds);
		}

		if(first)
		{
			rel_range->latT = way_range->latT;
			rel_range->lonL = way_range->lonL;
			rel_range->latB = way_range->latB;
			rel_range->lonR = way_range->lonR;

			first = 0;
		}
		else
		{
			if(way_range->latT > rel_range->latT)
			{
				rel_range->latT = way_range->latT;
			}

			if(way_range->lonL < rel_range->lonL)
			{
				rel_range->lonL = way_range->lonL;
			}

			if(way_range->latB < rel_range->latB)
			{
				rel_range->latB = way_range->latB;
			}

			if(way_range->lonR > rel_range->lonR)
			{
				rel_range->lonR = way_range->lonR;
			}
		}

		// hnd_way_range may be NULL
		osmdb_index_put(self->index, &hnd_way_range);
	}

	return 1;
}

static int
osm_parser_insertRel(osm_parser_t* self,
                     int center, int polygon, int min_zoom)
{
	ASSERT(self);

	size_t size;
	size = osmdb_relInfo_sizeof(self->rel_info);
	if(osmdb_index_add(self->index,
	                   OSMDB_TYPE_RELINFO,
	                   self->rel_info->rid,
	                   size, (void*) self->rel_info) == 0)
	{
		return 0;
	}

	if(osm_parser_computeRelRange(self) == 0)
	{
		return 0;
	}

	size = osmdb_relRange_sizeof(self->rel_range);
	if(osmdb_index_add(self->index,
	                   OSMDB_TYPE_RELRANGE,
	                   self->rel_range->rid,
	                   size, (void*) self->rel_range) == 0)
	{
		return 0;
	}

	// discard relation members which are centered
	// discard large polygon relation members
	// large areas are defined to be 50% of the area covered
	// by a "typical" zoom 14 tile. e.g.
	// 14/3403/6198:
	// latT=40.078071, lonL=-105.227051,
	// latB=40.061257, lonR=-105.205078,
	// area=0.000369
	double latT = self->rel_range->latT;
	double lonL = self->rel_range->lonL;
	double latB = self->rel_range->latB;
	double lonR = self->rel_range->lonR;
	float  area = (float) ((latT-latB)*(lonR-lonL));
	if((center == 0) &&
	   ((polygon == 0) ||
	    (polygon && (0.5f*area < 0.000369f))))
	{
		size = osmdb_relMembers_sizeof(self->rel_members);
		if(osmdb_index_add(self->index,
		                   OSMDB_TYPE_RELMEMBERS,
		                   self->rel_members->rid,
		                   size, (void*) self->rel_members) == 0)
		{
			return 0;
		}
	}

	if(osm_parser_addTileRange(self,
	                           OSMDB_TYPE_RELRANGE,
	                           self->rel_range->rid,
	                           latT, lonL,
	                           latB, lonR,
	                           center, polygon,
	                           min_zoom) == 0)
	{
		return 0;
	}

	return 1;
}

static int
osm_parser_endOsmRel(osm_parser_t* self, int line,
                     float progress, const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	int selected = 0;
	int center   = 0;
	int polygon  = 0;

	// select relations when a line/poly exists or
	// when a point and name exists
	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style,
	                       osmdb_classCodeToName(self->rel_info->class));
	if(sc && (sc->line || sc->poly))
	{
		if(sc->poly)
		{
			polygon = 1;
		}

		selected = 1;
	}
	else if(sc && sc->point && (self->tag_name[0] != '\0'))
	{
		selected = 1;
		center   = 1;
	}

	// discard relations when not selected
	// or if the type is not supported
	if((selected == 0) ||
	   (self->rel_info->type == OSMDB_RELINFO_TYPE_NONE))
	{
		return 1;
	}

	// fill the name
	if(self->tag_abrev[0] == '\0')
	{
		osmdb_relInfo_addName(self->rel_info,
		                      self->tag_name);
	}
	else
	{
		osmdb_relInfo_addName(self->rel_info,
		                      self->tag_abrev);
	}

	int min_zoom = sc ? osmdb_styleClass_minZoom(sc) : 999;
	if(osm_parser_insertRel(self, center,
	                        polygon, min_zoom) == 0)
	{
		return 0;
	}

	++self->count_rels;

	double dt;
	if(osm_parser_logProgress(self, &dt))
	{
		LOGI("dt=%0.0lf, progress=%f, memsize=%" PRId64 ", count=%" PRIu64,
		     dt, 100.0f*progress, (int64_t) MEMSIZE(), self->count_rels);
	}

	return 1;
}

static int
osm_parser_beginOsmRelTag(osm_parser_t* self, int line,
                          const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_REL_TAG;

	int  i = 0;
	int  j = 1;
	int  m = 2;
	int  n = 3;
	char val[256];
	while(atts[i] && atts[j] && atts[m] && atts[n])
	{
		if((strcmp(atts[i], "k") == 0) &&
		   (strcmp(atts[m], "v") == 0))
		{
			// iconv value
			osm_parser_iconv(self, atts[n], val);

			char name[256];
			char abrev[256];

			int class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				// set or overwrite generic class
				if((self->rel_info->class == self->class_none)   ||
				   (self->rel_info->class == self->building_yes) ||
				   (self->rel_info->class == self->barrier_yes)  ||
				   (self->rel_info->class == self->office_yes)   ||
				   (self->rel_info->class == self->historic_yes) ||
				   (self->rel_info->class == self->man_made_yes) ||
				   (self->rel_info->class == self->tourism_yes))
				{
					self->rel_info->class = class;
				}
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        (self->name_en == 0) &&
			        osm_parseName(line, val, name, abrev))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if((strcmp(atts[j], "name:en") == 0) &&
			        osm_parseName(line, val, name, abrev))
			{
				self->name_en = 1;
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if((strcmp(atts[j], "type") == 0))
			{
				self->rel_info->type = osmdb_relationTagTypeToCode(val);
			}
		}

		i += 4;
		j += 4;
		m += 4;
		n += 4;
	}

	return 1;
}

static int
osm_parser_endOsmRelTag(osm_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM_REL;

	return 1;
}

static int
osm_parser_beginOsmRelMember(osm_parser_t* self, int line,
                             const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSM_STATE_OSM_REL_MEMBER;

	// update members size
	if(self->rel_members_maxCount <= self->rel_members->count)
	{
		osmdb_relMembers_t tmp_size =
		{
			.count=2*self->rel_members_maxCount
		};

		size_t size = osmdb_relMembers_sizeof(&tmp_size);

		osmdb_relMembers_t* tmp;
		tmp = (osmdb_relMembers_t*)
		      REALLOC((void*) self->rel_members, size);
		if(tmp == NULL)
		{
			LOGE("REALLOC failed");
			return 0;
		}

		self->rel_members          = tmp;
		self->rel_members_maxCount = tmp_size.count;
	}

	// get the next member data
	osmdb_relData_t* data;
	data = osmdb_relMembers_data(self->rel_members);
	data = &(data[self->rel_members->count]);

	// parse the member
	int     i    = 0;
	int     j    = 1;
	int     type = 0;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "ref")  == 0)
		{
			data->ref = (int64_t) strtoll(atts[j], NULL, 0);
		}
		else if(strcmp(atts[i], "type")  == 0)
		{
			type = osmdb_relationMemberTypeToCode(atts[j]);
		}
		else if(strcmp(atts[i], "role")  == 0)
		{
			data->role = osmdb_relationMemberRoleToCode(atts[j]);
		}

		i += 2;
		j += 2;
	}

	// store the admin_centre or way member
	// ignore unsupported member types
	if((type == self->rel_member_type_node) &&
	   ((data->role == self->rel_member_role_admin_centre) ||
	    (data->role == self->rel_member_role_label)))
	{
		self->rel_info->nid = data->ref;
	}
	else if(type == self->rel_member_type_way)
	{
		self->rel_members->count += 1;
	}

	return 1;
}

static int
osm_parser_endOsmRelMember(osm_parser_t* self, int line,
                           const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM_REL;

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osm_parser_t*
osm_parser_new(const char* style,
               const char* db_name)
{
	ASSERT(style);
	ASSERT(db_name);

	osm_parser_t* self = (osm_parser_t*)
	                     CALLOC(1, sizeof(osm_parser_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->t0 = cc_timestamp();

	self->index = osmdb_index_new(db_name,
	                              OSMDB_INDEX_MODE_CREATE, 1);
	if(self->index == NULL)
	{
		goto fail_index;
	}

	self->style = osmdb_style_newFile(style);
	if(self->style == NULL)
	{
		goto fail_style;
	}

	osmdb_nodeCoord_t tmp_node_coord = { .nid=0 };
	size_t size = osmdb_nodeCoord_sizeof(&tmp_node_coord);
	self->node_coord = (osmdb_nodeCoord_t*)
	                   CALLOC(1, size);
	if(self->node_coord == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_node_coord;
	}

	osmdb_nodeInfo_t tmp_node_info = { .size_name=256 };
	size = osmdb_nodeInfo_sizeof(&tmp_node_info);
	self->node_info = (osmdb_nodeInfo_t*)
	                  CALLOC(1, size);
	if(self->node_info == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_node_info;
	}

	osmdb_wayInfo_t tmp_way_info = { .size_name=256 };
	size = osmdb_wayInfo_sizeof(&tmp_way_info);
	self->way_info = (osmdb_wayInfo_t*)
	                 CALLOC(1, size);
	if(self->way_info == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_way_info;
	}

	osmdb_wayRange_t tmp_way_range = { .wid=0 };
	size = osmdb_wayRange_sizeof(&tmp_way_range);
	self->way_range = (osmdb_wayRange_t*)
	                  CALLOC(1, size);
	if(self->way_range == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_way_range;
	}

	self->way_nds_maxCount = 256;
	osmdb_wayNds_t tmp_way_nds =
	{
		.count=self->way_nds_maxCount
	};

	size = osmdb_wayNds_sizeof(&tmp_way_nds);
	self->way_nds = (osmdb_wayNds_t*)
	                CALLOC(1, size);
	if(self->way_nds == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_way_nds;
	}

	osmdb_relInfo_t tmp_rel_info = { .size_name=256 };
	size = osmdb_relInfo_sizeof(&tmp_rel_info);
	self->rel_info = (osmdb_relInfo_t*)
	                 CALLOC(1, size);
	if(self->rel_info == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_rel_info;
	}

	osmdb_relRange_t tmp_rel_range = { .rid=0 };
	size = osmdb_relRange_sizeof(&tmp_rel_range);
	self->rel_range = (osmdb_relRange_t*)
	                  CALLOC(1, size);
	if(self->rel_range == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_rel_range;
	}

	self->rel_members_maxCount = 256;
	osmdb_relMembers_t tmp_rel_members =
	{
		.count=self->rel_members_maxCount
	};

	size = osmdb_relMembers_sizeof(&tmp_rel_members);
	self->rel_members = (osmdb_relMembers_t*)
	                    CALLOC(1, size);
	if(self->rel_members == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_rel_members;
	}

	self->class_map = cc_map_new();
	if(self->class_map == NULL)
	{
		goto fail_class_map;
	}

	if(osm_parser_fillClass(self) == 0)
	{
		goto fail_fill_class;
	}

	// initialize locale for iconv
	setlocale(LC_CTYPE, "");

	self->cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	if(self->cd == ICONV_OPEN_ERR)
	{
		LOGE("iconv_open failed");
		goto fail_iconv_open;
	}

	self->class_none   = osmdb_classKVToCode("class",    "none");
	self->building_yes = osmdb_classKVToCode("building", "yes");
	self->barrier_yes  = osmdb_classKVToCode("barrier",  "yes");
	self->office_yes   = osmdb_classKVToCode("office",   "yes");
	self->historic_yes = osmdb_classKVToCode("historic", "yes");
	self->man_made_yes = osmdb_classKVToCode("man_made", "yes");
	self->tourism_yes  = osmdb_classKVToCode("tourism",  "yes");

	self->rel_member_type_node         = osmdb_relationMemberTypeToCode("node");
	self->rel_member_type_way          = osmdb_relationMemberTypeToCode("way");
	self->rel_member_role_admin_centre = osmdb_relationMemberRoleToCode("admin_centre");
	self->rel_member_role_label        = osmdb_relationMemberRoleToCode("label");

	// success
	return self;

	// failure
	fail_iconv_open:
		osm_parser_discardClass(self);
	fail_fill_class:
		cc_map_delete(&self->class_map);
	fail_class_map:
		FREE(self->rel_members);
	fail_rel_members:
		FREE(self->rel_range);
	fail_rel_range:
		FREE(self->rel_info);
	fail_rel_info:
		FREE(self->way_nds);
	fail_way_nds:
		FREE(self->way_range);
	fail_way_range:
		FREE(self->way_info);
	fail_way_info:
		FREE(self->node_info);
	fail_node_info:
		FREE(self->node_coord);
	fail_node_coord:
		osmdb_style_delete(&self->style);
	fail_style:
		osmdb_index_delete(&self->index);
	fail_index:
		FREE(self);
	return NULL;
}

void osm_parser_delete(osm_parser_t** _self)
{
	ASSERT(_self);

	osm_parser_t* self = *_self;
	if(self)
	{
		iconv_close(self->cd);

		osm_parser_discardClass(self);
		cc_map_delete(&self->class_map);

		FREE(self->rel_members);
		FREE(self->rel_range);
		FREE(self->rel_info);
		FREE(self->way_nds);
		FREE(self->way_range);
		FREE(self->way_info);
		FREE(self->node_info);
		FREE(self->node_coord);

		osmdb_style_delete(&self->style);
		osmdb_index_delete(&self->index);

		FREE(self);
		*_self = NULL;
	}
}

int osm_parser_parseFile(osm_parser_t* self,
                         const char* fname)
{
	ASSERT(self);
	ASSERT(fname);

	if(xml_istream_parse((void*) self,
	                     osm_parser_start,
	                     osm_parser_end,
	                     fname) == 0)
	{
		return 0;
	}

	return 1;
}

int osm_parser_start(void* priv, int line, float progress,
                     const char* name, const char** atts)
{
	ASSERT(priv);
	ASSERT(name);
	ASSERT(atts);

	osm_parser_t* self = (osm_parser_t*) priv;

	int state = self->state;
	if(state == OSM_STATE_INIT)
	{
		if(strcmp(name, "osm") == 0)
		{
			return osm_parser_beginOsm(self, line, atts);
		}
	}
	else if(state == OSM_STATE_OSM)
	{
		if(strcmp(name, "bounds") == 0)
		{
			return osm_parser_beginOsmBounds(self, line, atts);
		}
		else if(strcmp(name, "node") == 0)
		{
			return osm_parser_beginOsmNode(self, line, atts);
		}
		else if(strcmp(name, "way") == 0)
		{
			return osm_parser_beginOsmWay(self, line, atts);
		}
		else if(strcmp(name, "relation") == 0)
		{
			return osm_parser_beginOsmRel(self, line, atts);
		}
	}
	else if(state == OSM_STATE_OSM_NODE)
	{
		if(strcmp(name, "tag") == 0)
		{
			return osm_parser_beginOsmNodeTag(self, line, atts);
		}
	}
	else if(state == OSM_STATE_OSM_WAY)
	{
		if(strcmp(name, "tag") == 0)
		{
			return osm_parser_beginOsmWayTag(self, line, atts);
		}
		else if(strcmp(name, "nd") == 0)
		{
			return osm_parser_beginOsmWayNd(self, line, atts);
		}
	}
	else if(state == OSM_STATE_OSM_REL)
	{
		if(strcmp(name, "tag") == 0)
		{
			return osm_parser_beginOsmRelTag(self, line, atts);
		}
		else if(strcmp(name, "member") == 0)
		{
			return osm_parser_beginOsmRelMember(self, line, atts);
		}
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

int osm_parser_end(void* priv, int line, float progress,
                   const char* name, const char* content)
{
	// content may be NULL
	ASSERT(priv);
	ASSERT(name);

	osm_parser_t* self = (osm_parser_t*) priv;

	int state = self->state;
	if(state == OSM_STATE_OSM)
	{
		return osm_parser_endOsm(self, line, content);
	}
	else if(state == OSM_STATE_OSM_BOUNDS)
	{
		return osm_parser_endOsmBounds(self, line, content);
	}
	else if(state == OSM_STATE_OSM_NODE)
	{
		return osm_parser_endOsmNode(self, line,
		                             progress, content);
	}
	else if(state == OSM_STATE_OSM_WAY)
	{
		return osm_parser_endOsmWay(self, line,
		                            progress, content);
	}
	else if(state == OSM_STATE_OSM_REL)
	{
		return osm_parser_endOsmRel(self, line,
		                            progress, content);
	}
	else if(state == OSM_STATE_OSM_NODE_TAG)
	{
		return osm_parser_endOsmNodeTag(self, line, content);
	}
	else if(state == OSM_STATE_OSM_WAY_TAG)
	{
		return osm_parser_endOsmWayTag(self, line, content);
	}
	else if(state == OSM_STATE_OSM_WAY_ND)
	{
		return osm_parser_endOsmWayNd(self, line, content);
	}
	else if(state == OSM_STATE_OSM_REL_TAG)
	{
		return osm_parser_endOsmRelTag(self, line, content);
	}
	else if(state == OSM_STATE_OSM_REL_MEMBER)
	{
		return osm_parser_endOsmRelMember(self, line, content);
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

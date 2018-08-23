/*
 * Copyright (c) 2018 Jeff Boody
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
#include "libxmlstream/xml_istream.h"
#include "libxmlstream/xml_ostream.h"
#include "osm_parser.h"
#include "../osmdb_util.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

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

/***********************************************************
* private - auxillary data                                 *
***********************************************************/

typedef struct
{
	int    type;
	int    role;
	double ref;
} osm_relationMember_t;

static osm_relationMember_t*
osm_relationMember_new(int type, int role, double ref)
{
	osm_relationMember_t* self = (osm_relationMember_t*)
	                             malloc(sizeof(osm_relationMember_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->type = type;
	self->role = role;
	self->ref  = ref;

	return self;
}

static void
osm_relationMember_delete(osm_relationMember_t** _self)
{
	assert(_self);

	osm_relationMember_t* self = *_self;
	if(self)
	{
		free(self);
		*_self = NULL;
	}
}

typedef struct
{
	double lat;
	double lon;
} osm_coord_t;

static osm_coord_t* osm_coord_new(double lat, double lon)
{
	osm_coord_t* self = (osm_coord_t*)
	                    malloc(sizeof(osm_coord_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}

	self->lat = lat;
	self->lon = lon;
	return self;
}

static void osm_coord_delete(osm_coord_t** _self)
{
	assert(_self);

	osm_coord_t* self = *_self;
	if(self)
	{
		free(self);
		*_self = NULL;
	}
}

typedef struct
{
	double t;
	double l;
	double b;
	double r;
} osm_box_t;

static osm_box_t* osm_box_newNds(a3d_list_t* nds,
                                 a3d_hashmap_t* nodes)
{
	assert(nds);
	assert(nodes);

	osm_box_t*      self = NULL;
	a3d_listitem_t* iter = a3d_list_head(nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_peekitem(iter);

		// check if ref exists in nodes
		a3d_hashmapIter_t hiterator;
		osm_coord_t* coord;
		coord = (osm_coord_t*)
		        a3d_hashmap_findf(nodes, &hiterator,
		                          "%0.0lf", *ref);
		if(coord == NULL)
		{
			// filtered in osm_parser_beginOsmWayNd
			LOGW("invalid ref=%0.0lf", *ref);
			iter = a3d_list_next(iter);
			continue;
		}

		if(self == NULL)
		{
			self = (osm_box_t*)
			       malloc(sizeof(osm_box_t));
			if(self == NULL)
			{
				LOGE("malloc failed");
				return NULL;
			}

			// bounding box is a single point
			self->t = coord->lat;
			self->l = coord->lon;
			self->b = coord->lat;
			self->r = coord->lon;
		}
		else
		{
			// include the new point in the bounding box
			if(coord->lat > self->t)
			{
				self->t = coord->lat;
			}
			else if(coord->lat < self->b)
			{
				self->b = coord->lat;
			}

			if(coord->lon < self->l)
			{
				self->l = coord->lon;
			}
			else if(coord->lon > self->r)
			{
				self->r = coord->lon;
			}
		}

		iter = a3d_list_next(iter);
	}

	return self;
}

static osm_box_t*
osm_box_newMembers(a3d_list_t* rel_members,
                   a3d_hashmap_t* nodes,
                   a3d_hashmap_t* ways)
{
	assert(rel_members);
	assert(nodes);
	assert(ways);

	// write rel members
	osm_box_t*      self = NULL;
	a3d_listitem_t* iter = a3d_list_head(rel_members);
	while(iter)
	{
		osm_relationMember_t* m;
		m = (osm_relationMember_t*)
		    a3d_list_peekitem(iter);

		// check if ref exists in nodes
		a3d_hashmapIter_t hiterator;
		osm_coord_t* coord = NULL;
		osm_box_t*   box   = NULL;
		const char*  type  = osmdb_relationMemberCodeToType(m->type);
		if(strcmp("node", type) == 0)
		{
			coord = (osm_coord_t*)
			        a3d_hashmap_findf(nodes, &hiterator,
			                          "%0.0lf", m->ref);
		}
		else if(strcmp("way", type) == 0)
		{
			box = (osm_box_t*)
			        a3d_hashmap_findf(ways, &hiterator,
			                          "%0.0lf", m->ref);
		}
		else
		{
			// filtered in osm_parser_beginOsmRelMember
			LOGW("ignore ref=%0.0lf", m->ref);
			iter = a3d_list_next(iter);
			continue;
		}

		if((coord == NULL) && (box == NULL))
		{
			// filtered in osm_parser_beginOsmRelMember
			LOGW("invalid ref=%0.0lf", m->ref);
			iter = a3d_list_next(iter);
			continue;
		}

		if(self == NULL)
		{
			self = (osm_box_t*)
			       malloc(sizeof(osm_box_t));
			if(self == NULL)
			{
				LOGE("malloc failed");
				return NULL;
			}

			// initialize bounding box
			if(coord)
			{
				self->t = coord->lat;
				self->l = coord->lon;
				self->b = coord->lat;
				self->r = coord->lon;
			}
			else
			{
				self->t = box->t;
				self->l = box->l;
				self->b = box->b;
				self->r = box->r;
			}
		}
		else
		{
			// include the new coord/box in the bounding box
			if(coord)
			{
				if(coord->lat > self->t)
				{
					self->t = coord->lat;
				}
				else if(coord->lat < self->b)
				{
					self->b = coord->lat;
				}

				if(coord->lon < self->l)
				{
					self->l = coord->lon;
				}
				else if(coord->lon > self->r)
				{
					self->r = coord->lon;
				}
			}
			else
			{
				if(box->t > self->t)
				{
					self->t = box->t;
				}
				else if(box->b < self->b)
				{
					self->b = box->b;
				}

				if(box->l < self->l)
				{
					self->l = box->l;
				}
				else if(box->r > self->r)
				{
					self->r = box->r;
				}
			}
		}

		iter = a3d_list_next(iter);
	}

	return self;
}

static void osm_box_delete(osm_box_t** _self)
{
	assert(_self);

	osm_box_t* self = *_self;
	if(self)
	{
		free(self);
		*_self = NULL;
	}
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
	assert(a);
	assert(b);

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
	assert(str);
	assert(word);

	strncat(str, word, 256);
	str[255] = '\0';
}

static const char* osm_parseWord(int line,
                                 const char* str,
                                 osm_token_t* tok)
{
	assert(str);
	assert(tok);

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
		if((c == '\n') ||
		   (c == '\t') ||
		   (c == '\r') ||
		   (c == '"'))
		{
			// eat unsupported characters
			if(c == '"')
			{
				LOGW("quote i=%i, line=%i, str=%s",
				     i, line, str);
			}
			++i;
			continue;
		}
		else if(((c >= 32) && (c <= 126)) ||
		        (c == '\0'))
		{
			// accept printable characters and null char
		}
		else
		{
			// eat invalid characters
			LOGW("invalid line=%i, c=0x%X, str=%s", line, (unsigned int) c, str);
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
			LOGW("invalid line=%i",line);
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

static int osm_parseName(int line,
                         const char* input,
                         char* name,
                         char* abrev)
{
	assert(input);
	assert(name);
	assert(abrev);

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
		LOGW("trim %s", input);
		words -= 2;
	}

	if(words == 0)
	{
		// input is null string
		LOGW("invalid line=%i, name=%s", line, input);
		return 0;
	}
	else if(words == 1)
	{
		// input is single word (don't abreviate)
		strncpy(name, input, 256);
		name[255] = '\0';
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
	assert(a);

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
		LOGW("invalid line=%i, ele=%s", line, a);
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
			LOGW("invalid line=%i, ele=%s", line, a);
			return 0;
		}
	}

	LOGW("invalid line=%i, ele=%s", line, a);
	return 0;
}

static int osm_parseSt(const char* num)
{
	assert(num);

	int code = (int) strtol(num, NULL, 10);
	if((code < 0) || (code >= 60))
	{
		return 0;
	}

	// replace empty string with 0
	const char* abrev = osmdb_stCodeToAbrev(code);
	if(abrev[0] == '\0')
	{
		return 0;
	}

	return code;
}

/***********************************************************
* private                                                  *
***********************************************************/

static void osm_parser_init(osm_parser_t* self)
{
	assert(self);

	self->attr_id      = 0.0;
	self->attr_lat     = 0.0;
	self->attr_lon     = 0.0;
	self->tag_name[0]  = '\0';
	self->tag_abrev[0] = '\0';
	self->tag_ele      = 0;
	self->tag_st       = 0;
	self->tag_class    = 0;
}

static int
osm_parser_beginOsm(osm_parser_t* self, int line,
                    const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM;
	xml_ostream_begin(self->os, "osmdb");

	return 1;
}

static int
osm_parser_endOsm(osm_parser_t* self, int line,
                  const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_DONE;
	xml_ostream_end(self->os);

	return 1;
}

static int
osm_parser_beginOsmBounds(osm_parser_t* self, int line,
                          const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_BOUNDS;

	return 1;
}

static int
osm_parser_endOsmBounds(osm_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_OSM;

	return 1;
}

static int
osm_parser_beginOsmNode(osm_parser_t* self, int line,
                        const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_NODE;
	xml_ostream_begin(self->os, "node");
	osm_parser_init(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->attr_id = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "lat") == 0)
		{
			self->attr_lat = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "lon") == 0)
		{
			self->attr_lon = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_endOsmNode(osm_parser_t* self, int line,
                      const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_OSM;

	osm_coord_t* coord;
	coord = osm_coord_new(self->attr_lat, self->attr_lon);
	if(coord == NULL)
	{
		return 0;
	}

	a3d_hashmapIter_t hiterator;
	if(a3d_hashmap_addf(self->nodes, &hiterator,
	                    (const void*) coord,
	                    "%0.0lf", self->attr_id) == 0)
	{
		osm_coord_delete(&coord);
		return 0;
	}

	xml_ostream_attrf(self->os, "id", "%0.0lf", self->attr_id);
	if((self->attr_lat != 0.0) || (self->attr_lon != 0.0))
	{
		xml_ostream_attrf(self->os, "lat", "%lf", self->attr_lat);
		xml_ostream_attrf(self->os, "lon", "%lf", self->attr_lon);
	}
	if(self->tag_name[0] != '\0')
	{
		xml_ostream_attr(self->os, "name", self->tag_name);
	}
	if(self->tag_abrev[0] != '\0')
	{
		xml_ostream_attr(self->os, "abrev", self->tag_abrev);
	}
	if(self->tag_ele)
	{
		xml_ostream_attrf(self->os, "ele", "%i", self->tag_ele);
	}
	if(self->tag_st)
	{
		xml_ostream_attr(self->os, "st",
		                 osmdb_stCodeToAbrev(self->tag_st));
	}
	if(self->tag_class)
	{
		xml_ostream_attr(self->os, "class",
		                 osmdb_classCodeToName(self->tag_class));
	}
	xml_ostream_end(self->os);

	return 1;
}

static int
osm_parser_beginOsmNodeTag(osm_parser_t* self, int line,
                           const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_NODE_TAG;

	int i = 0;
	int j = 1;
	int m = 2;
	int n = 3;
	while(atts[i] && atts[j] && atts[m] && atts[n])
	{
		if((strcmp(atts[i], "k") == 0) &&
		   (strcmp(atts[m], "v") == 0))
		{
			char name[256];
			char abrev[256];
			int  class = osmdb_classKVToCode(atts[j], atts[n]);
			if(class)
			{
				self->tag_class = class;
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        osm_parseName(line, atts[n], name, abrev))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if(strcmp(atts[j], "ele:ft") == 0)
			{
				self->tag_ele = osm_parseEle(line, atts[n], 1);
			}
			else if(strcmp(atts[j], "ele") == 0)
			{
				self->tag_ele = osm_parseEle(line, atts[n], 0);
			}
			else if((strcmp(atts[j], "gnis:ST_num")   == 0) ||
			        (strcmp(atts[j], "gnis:state_id") == 0))
			{
				self->tag_st = osm_parseSt(atts[n]);
			}
			else if(strcmp(atts[j], "gnis:ST_alpha") == 0)
			{
				self->tag_st = osmdb_stAbrevToCode(atts[n]);
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
	assert(self);

	self->state = OSM_STATE_OSM_NODE;

	return 1;
}

static int
osm_parser_beginOsmWay(osm_parser_t* self, int line,
                       const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_WAY;
	xml_ostream_begin(self->os, "way");
	osm_parser_init(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->attr_id = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_endOsmWay(osm_parser_t* self, int line,
                     const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_OSM;

	osm_box_t* box;
	box = osm_box_newNds(self->way_nds, self->nodes);
	if(box == NULL)
	{
		LOGE("invalid box on line=%i", line);
		return 0;
	}

	a3d_hashmapIter_t hiterator;
	if(a3d_hashmap_addf(self->ways, &hiterator,
	                    (const void*) box,
	                    "%0.0lf", self->attr_id) == 0)
	{
		osm_box_delete(&box);
		return 0;
	}

	xml_ostream_attrf(self->os, "id", "%0.0lf", self->attr_id);
	if(self->tag_name[0] != '\0')
	{
		xml_ostream_attr(self->os, "name", self->tag_name);
	}
	if(self->tag_abrev[0] != '\0')
	{
		xml_ostream_attr(self->os, "abrev", self->tag_abrev);
	}
	if(self->tag_class)
	{
		xml_ostream_attr(self->os, "class",
		                 osmdb_classCodeToName(self->tag_class));
	}
	if(box)
	{
		xml_ostream_attrf(self->os, "t", "%lf", box->t);
		xml_ostream_attrf(self->os, "l", "%lf", box->l);
		xml_ostream_attrf(self->os, "b", "%lf", box->b);
		xml_ostream_attrf(self->os, "r", "%lf", box->r);
	}

	// write way nds
	a3d_listitem_t* iter = a3d_list_head(self->way_nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_remove(self->way_nds, &iter);

		xml_ostream_begin(self->os, "nd");
		xml_ostream_attrf(self->os, "ref", "%0.0lf", *ref);
		xml_ostream_end(self->os);
		free(ref);
	}
	xml_ostream_end(self->os);

	return 1;
}

static int
osm_parser_beginOsmWayTag(osm_parser_t* self, int line,
                          const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_WAY_TAG;

	int i = 0;
	int j = 1;
	int m = 2;
	int n = 3;
	while(atts[i] && atts[j] && atts[m] && atts[n])
	{
		if((strcmp(atts[i], "k") == 0) &&
		   (strcmp(atts[m], "v") == 0))
		{
			char name[256];
			char abrev[256];
			int  class = osmdb_classKVToCode(atts[j], atts[n]);
			if(class)
			{
				self->tag_class = class;
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        osm_parseName(line, atts[n], name, abrev))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
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
	assert(self);

	self->state = OSM_STATE_OSM_WAY;

	return 1;
}

static int
osm_parser_beginOsmWayNd(osm_parser_t* self, int line,
                         const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_WAY_ND;

	double* ref = (double*) malloc(sizeof(double));
	if(ref == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	*ref = 0.0;

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "ref")  == 0)
		{
			*ref = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	// check if ref exists in nodes
	a3d_hashmapIter_t hiterator;
	if(a3d_hashmap_findf(self->nodes, &hiterator,
	                     "%0.0lf", *ref) == NULL)
	{
		// LOGW("discard ref=%0.0lf", *ref);
		free(ref);
		return 1;
	}

	// add the ref to the way_nds
	if((*ref == 0.0) ||
	   (a3d_list_enqueue(self->way_nds,
	                     (const void*) ref) == 0))
	{
		free(ref);
		return 0;
	}

	return 1;
}

static int
osm_parser_endOsmWayNd(osm_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_OSM_WAY;

	return 1;
}

static int
osm_parser_beginOsmRel(osm_parser_t* self, int line,
                       const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_REL;
	xml_ostream_begin(self->os, "relation");
	osm_parser_init(self);

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "id")  == 0)
		{
			self->attr_id = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	return 1;
}

static int
osm_parser_endOsmRel(osm_parser_t* self, int line,
                     const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_OSM;

	osm_box_t* box;
	box = osm_box_newMembers(self->rel_members, self->nodes, self->ways);
	if(box == NULL)
	{
		LOGE("invalid box on line=%i", line);
		return 0;
	}

	a3d_hashmapIter_t hiterator;
	if(a3d_hashmap_addf(self->rels, &hiterator,
	                    (const void*) box,
	                    "%0.0lf", self->attr_id) == 0)
	{
		osm_box_delete(&box);
		return 0;
	}

	xml_ostream_attrf(self->os, "id", "%0.0lf", self->attr_id);
	if(self->tag_name[0] != '\0')
	{
		xml_ostream_attr(self->os, "name", self->tag_name);
	}
	if(self->tag_abrev[0] != '\0')
	{
		xml_ostream_attr(self->os, "abrev", self->tag_abrev);
	}
	if(self->rel_type)
	{
		xml_ostream_attr(self->os, "type",
		                 osmdb_relationTagCodeToType(self->rel_type));
	}
	if(self->tag_class)
	{
		xml_ostream_attr(self->os, "class",
		                 osmdb_classCodeToName(self->tag_class));
	}
	if(box)
	{
		xml_ostream_attrf(self->os, "t", "%lf", box->t);
		xml_ostream_attrf(self->os, "l", "%lf", box->l);
		xml_ostream_attrf(self->os, "b", "%lf", box->b);
		xml_ostream_attrf(self->os, "r", "%lf", box->r);
	}

	// write rel members
	a3d_listitem_t* iter = a3d_list_head(self->rel_members);
	while(iter)
	{
		osm_relationMember_t* m;
		m = (osm_relationMember_t*)
		    a3d_list_remove(self->rel_members, &iter);
		if(m->type && m->role && (m->ref != 0.0))
		{
			xml_ostream_begin(self->os, "member");
			xml_ostream_attr(self->os, "type",
			                 osmdb_relationMemberCodeToType(m->type));
			xml_ostream_attrf(self->os, "ref", "%0.0lf", m->ref);
			xml_ostream_attr(self->os, "role",
			                 osmdb_relationMemberCodeToRole(m->role));
			xml_ostream_end(self->os);
		}
		osm_relationMember_delete(&m);
	}
	xml_ostream_end(self->os);

	return 1;
}

static int
osm_parser_beginOsmRelTag(osm_parser_t* self, int line,
                          const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_REL_TAG;

	int i = 0;
	int j = 1;
	int m = 2;
	int n = 3;
	while(atts[i] && atts[j] && atts[m] && atts[n])
	{
		if((strcmp(atts[i], "k") == 0) &&
		   (strcmp(atts[m], "v") == 0))
		{
			char name[256];
			char abrev[256];
			int  class = osmdb_classKVToCode(atts[j], atts[n]);
			if(class)
			{
				self->tag_class = class;
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        osm_parseName(line, atts[n], name, abrev))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
			}
			else if((strcmp(atts[j], "type") == 0))
			{
				self->rel_type = osmdb_relationTagTypeToCode(atts[n]);
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
	assert(self);

	self->state = OSM_STATE_OSM_REL;

	return 1;
}

static int
osm_parser_beginOsmRelMember(osm_parser_t* self, int line,
                             const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSM_STATE_OSM_REL_MEMBER;

	int    i    = 0;
	int    j    = 1;
	int    type = 0;
	double ref  = 0.0;
	int    role = 0;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "type")  == 0)
		{
			type = osmdb_relationMemberTypeToCode(atts[j]);
		}
		else if(strcmp(atts[i], "ref")  == 0)
		{
			ref = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "role")  == 0)
		{
			role = osmdb_relationMemberRoleToCode(atts[j]);
		}

		i += 2;
		j += 2;
	}

	// check if ref exists
	int type_node = osmdb_relationMemberTypeToCode("node");
	int type_way  = osmdb_relationMemberTypeToCode("way");
	a3d_hashmapIter_t hiterator;
	if(type == type_node)
	{
		if(a3d_hashmap_findf(self->nodes, &hiterator,
		                     "%0.0lf", ref) == NULL)
		{
			// LOGW("discard ref=%0.0lf", ref);
			return 1;
		}
	}
	else if(type == type_way)
	{
		if(a3d_hashmap_findf(self->ways, &hiterator,
		                     "%0.0lf", ref) == NULL)
		{
			// LOGW("discard ref=%0.0lf", ref);
			return 1;
		}
	}
	else
	{
		// ignore relations
		// LOGW("discard ref=%0.0lf", *ref);
		return 1;
	}

	osm_relationMember_t* m;
	m = osm_relationMember_new(type, role, ref);
	if(m == NULL)
	{
		return 0;
	}

	if(a3d_list_enqueue(self->rel_members,
	                    (const void*) m) == 0)
	{
		goto fail_enqueue;
		return 0;
	}

	// success
	return 1;

	// failure
	fail_enqueue:
		osm_relationMember_delete(&m);
	return 0;
}

static int
osm_parser_endOsmRelMember(osm_parser_t* self, int line,
                           const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSM_STATE_OSM_REL;

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osm_parser_t* osm_parser_new(xml_ostream_t* os)
{
	assert(os);

	osm_parser_t* self = (osm_parser_t*)
	                     calloc(1, sizeof(osm_parser_t));
	if(self == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	self->way_nds = a3d_list_new();
	if(self->way_nds == NULL)
	{
		goto fail_way_nds;
	}

	self->rel_members = a3d_list_new();
	if(self->rel_members == NULL)
	{
		goto fail_rel_members;
	}

	self->nodes = a3d_hashmap_new();
	if(self->nodes == NULL)
	{
		goto fail_nodes;
	}

	self->ways = a3d_hashmap_new();
	if(self->ways == NULL)
	{
		goto fail_ways;
	}

	self->rels = a3d_hashmap_new();
	if(self->rels == NULL)
	{
		goto fail_rels;
	}

	self->os = os;

	// success
	return self;

	// failure
	fail_rels:
		a3d_hashmap_delete(&self->ways);
	fail_ways:
		a3d_hashmap_delete(&self->nodes);
	fail_nodes:
		a3d_list_delete(&self->rel_members);
	fail_rel_members:
		a3d_list_delete(&self->way_nds);
	fail_way_nds:
		free(self);
	return NULL;
}

void osm_parser_delete(osm_parser_t** _self)
{
	assert(_self);

	osm_parser_t* self = *_self;
	if(self)
	{
		a3d_listitem_t* iter = a3d_list_head(self->rel_members);
		while(iter)
		{
			osm_relationMember_t* m;
			m = (osm_relationMember_t*)
			    a3d_list_remove(self->rel_members, &iter);
			osm_relationMember_delete(&m);
		}

		iter = a3d_list_head(self->way_nds);
		while(iter)
		{
			double* ref = (double*)
			              a3d_list_remove(self->way_nds, &iter);
			free(ref);
		}

		a3d_hashmapIter_t  hiterator;
		a3d_hashmapIter_t* hiter;
		hiter = a3d_hashmap_head(self->nodes, &hiterator);
		while(hiter)
		{
			osm_coord_t* coord;
			coord = (osm_coord_t*)
			        a3d_hashmap_remove(self->nodes, &hiter);
			osm_coord_delete(&coord);
		}

		hiter = a3d_hashmap_head(self->ways, &hiterator);
		while(hiter)
		{
			osm_box_t* box;
			box = (osm_box_t*)
			      a3d_hashmap_remove(self->ways, &hiter);
			osm_box_delete(&box);
		}

		hiter = a3d_hashmap_head(self->rels, &hiterator);
		while(hiter)
		{
			osm_box_t* box;
			box = (osm_box_t*)
			      a3d_hashmap_remove(self->rels, &hiter);
			osm_box_delete(&box);
		}

		a3d_hashmap_delete(&self->rels);
		a3d_hashmap_delete(&self->ways);
		a3d_hashmap_delete(&self->nodes);
		a3d_list_delete(&self->rel_members);
		a3d_list_delete(&self->way_nds);
		free(self);
		*_self = NULL;
	}
}

int osm_parser_start(void* priv,
                     int line,
                     const char* name,
                     const char** atts)
{
	assert(priv);
	assert(name);
	assert(atts);

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

int osm_parser_end(void* priv,
                   int line,
                   const char* name,
                   const char* content)
{
	// content may be NULL
	assert(priv);
	assert(name);

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
		return osm_parser_endOsmNode(self, line, content);
	}
	else if(state == OSM_STATE_OSM_WAY)
	{
		return osm_parser_endOsmWay(self, line, content);
	}
	else if(state == OSM_STATE_OSM_REL)
	{
		return osm_parser_endOsmRel(self, line, content);
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

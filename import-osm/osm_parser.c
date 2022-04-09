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
#include "libbfs/bfs_util.h"
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

static const char* OSM_NOCAPS_ARRAY[] =
{
	"a",
	"an",
	"and",
	"at",
	"by",
	"cdt",
	"du",
	"e",
	"el",
	"em",
	"en",
	"de",
	"del",
	"des",
	"ft",
	"for",
	"in",
	"l",
	"la",
	"las",
	"ll",
	"los",
	"n",
	"nd",
	"near",
	"o",
	"on",
	"of",
	"our",
	"rd",
	"s",
	"st",
	"t",
	"th",
	"the",
	"to",
	"via",
	"with",
	"y",
	NULL
};

typedef struct
{
	const char* from;
	const char* to;
} osm_abrev_t;

// abreviations based loosely on
// https://github.com/nvkelso/map-label-style-manual
// http://pe.usps.gov/text/pub28/28c1_001.htm
const osm_abrev_t OSM_ABREV_ARRAY[] =
{
	{ .from="North",      .to="N"     },
	{ .from="East",       .to="E"     },
	{ .from="South",      .to="S"     },
	{ .from="West",       .to="W"     },
	{ .from="Northeast",  .to="NE"    },
	{ .from="Northwest",  .to="NW"    },
	{ .from="Southeast",  .to="SE"    },
	{ .from="Southwest",  .to="SW"    },
	{ .from="Avenue",     .to="Ave"   },
	{ .from="Boulevard",  .to="Blvd"  },
	{ .from="Court",      .to="Ct"    },
	{ .from="Circle",     .to="Cir"   },
	{ .from="Drive",      .to="Dr"    },
	{ .from="Expressway", .to="Expwy" },
	{ .from="Freeway",    .to="Fwy"   },
	{ .from="Highway",    .to="Hwy"   },
	{ .from="Lane",       .to="Ln"    },
	{ .from="Parkway",    .to="Pkwy"  },
	{ .from="Place",      .to="Pl"    },
	{ .from="Road",       .to="Rd"    },
	{ .from="Street",     .to="St"    },
	{ .from="Terrace",    .to="Ter"   },
	{ .from="Trail",      .to="Tr"    },
	{ .from="Mount",      .to="Mt"    },
	{ .from="Mt.",        .to="Mt"    },
	{ .from="Mountain",   .to="Mtn"   },
	{ .from="Trailhead",  .to="TH"    },
	{ .from="Building",   .to="Bldg"  },
	{ .from="Campground", .to="CG"    },
	{ .from=NULL,         .to=NULL    },
};

/***********************************************************
* private - class utils                                    *
***********************************************************/

static void osm_parser_discardClass(osm_parser_t* self)
{
	ASSERT(self);

	cc_mapIter_t* iter;
	iter = cc_map_head(self->class_map);
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

	int* cls = NULL;

	cc_mapIter_t* miter;
	miter = cc_map_findf(self->class_map, "%s:%s", key, val);
	if(miter)
	{
		cls = (int*) cc_map_val(miter);
	}

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
		              osmdb_classCodeToName(i)) == NULL)
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

static int
osm_parser_fillNocaps(osm_parser_t* self)
{
	ASSERT(self);

	int idx = 0;
	while(OSM_NOCAPS_ARRAY[idx])
	{
		if(cc_map_add(self->nocaps_map,
		              (const void*) OSM_NOCAPS_ARRAY[idx],
		              OSM_NOCAPS_ARRAY[idx]) == NULL)
		{
			return 0;
		}

		++idx;
	}

	return 1;
}

static int
osm_parser_fillAbrev(osm_parser_t* self)
{
	ASSERT(self);

	int idx = 0;
	while(OSM_ABREV_ARRAY[idx].from)
	{
		if(cc_map_add(self->abrev_map,
		              (const void*) OSM_ABREV_ARRAY[idx].to,
		              OSM_ABREV_ARRAY[idx].from) == NULL)
		{
			return 0;
		}

		++idx;
	}

	return 1;
}

/***********************************************************
* private - parsing utils                                  *
***********************************************************/

typedef struct
{
	int abreviate;
	char word[256];
	char abrev[256];
	char sep[256];
} osm_token_t;

static void osm_catWord(char* str, char* word)
{
	ASSERT(str);
	ASSERT(word);

	strncat(str, word, 256);
	str[255] = '\0';
}

static void
osm_parser_capitolizeWord(osm_parser_t* self,
                          char* word)
{
	ASSERT(self);
	ASSERT(word);

	// capitolize the first letter
	if(cc_map_find(self->nocaps_map, word) == NULL)
	{
		char c = word[0];
		if((c >= 'a') && (c <= 'z'))
		{
			word[0] = c - 'a' + 'A';
		}
	}
}

static int
osm_parser_abreviateWord(osm_parser_t* self,
                         const char* word, char* abrev)
{
	ASSERT(self);
	ASSERT(word);
	ASSERT(abrev);

	int abreviate = 1;

	// abreviate selected words
	cc_mapIter_t* miter = cc_map_find(self->abrev_map, word);
	if(miter)
	{
		strncat(abrev, (const char*) cc_map_val(miter), 256);
	}
	else
	{
		strncat(abrev, word, 256);
		abreviate = 0;
	}
	abrev[255] = '\0';

	return abreviate;
}

static const char*
osm_parser_parseWord(osm_parser_t* self,
                     int line, int first,
                     const char* str, osm_token_t* tok)
{
	ASSERT(self);
	ASSERT(str);
	ASSERT(tok);

	tok->abreviate = 0;
	tok->word[0]   = '\0';
	tok->abrev[0]  = '\0';
	tok->sep[0]    = '\0';

	// eat whitespace
	int i = 0;
	while(1)
	{
		char c = str[i];
		if(first)
		{
			if((c == ' ')  ||
			   (c == '\n') ||
			   (c == '\t') ||
			   (c == '\r'))
			{
				++i;
				continue;
			}
		}
		else
		{
			if((c == '\n') ||
			   (c == '\t') ||
			   (c == '\r'))
			{
				++i;
				continue;
			}
		}

		break;
	}

	// find a word
	int len = 0;
	while(1)
	{
		char c = str[i];

		// validate len
		if(len == 255)
		{
			// LOGW("invalid line=%i",line);
			return NULL;
		}
		else if((len == 0) && (c == '\0'))
		{
			return NULL;
		}

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
		if(((c >= 'a') && (c <= 'z')) ||
		   ((c >= 'A') && (c <= 'Z')))
		{
			// append character to word
			tok->word[len]     = c;
			tok->word[len + 1] = '\0';
			++len;
			++i;
		}
		else if(c == '\0')
		{
			osm_parser_capitolizeWord(self, tok->word);
			tok->abreviate = osm_parser_abreviateWord(self,
			                                          tok->word,
			                                          tok->abrev);
			return &str[i];
		}
		else
		{
			osm_parser_capitolizeWord(self, tok->word);
			tok->abreviate = osm_parser_abreviateWord(self,
			                                          tok->word,
			                                          tok->abrev);
			break;
		}
	}

	// find a sep
	len = 0;
	while(1)
	{
		char c = str[i];

		// validate len
		if(len == 255)
		{
			// LOGW("invalid line=%i",line);
			return NULL;
		}

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
		else if((c == '.') &&
		        ((tok->word[0] < '0') ||
		         (tok->word[0] > '9')))
		{
			// disalow '.' for non-numbers
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
		if(((c >= 'a') && (c <= 'z')) ||
		   ((c >= 'A') && (c <= 'Z')) ||
		   (c == '\0'))
		{
			break;
		}
		else
		{
			// append character to sep
			tok->sep[len]     = c;
			tok->sep[len + 1] = '\0';
			++len;
			++i;
		}
	}

	return &str[i];
}

static int
osm_parser_parseName(osm_parser_t* self,
                     int line, const char* input,
                     char* name, char* abrev)
{
	ASSERT(self);
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
	int         first = 1;
	osm_token_t word[WORDS];
	while(str && (words < WORDS))
	{
		str = osm_parser_parseWord(self, line, first,
		                           str, &word[words]);
		if(str)
		{
			++words;
			first = 0;
		}
	}

	if(words >= 3)
	{
		if((strncmp(word[words - 3].word, "Multi", 256) == 0) &&
		   (strncmp(word[words - 2].word, "Use",   256) == 0) &&
		   (strncmp(word[words - 1].word, "Path",  256) == 0))
		{
			// abreviate Multi Use Path or Multi-Use Path
			osm_token_t* tmp = &word[words - 3];
			snprintf(tmp->word,  256, "%s", "MUP");
			snprintf(tmp->abrev, 256, "%s", "MUP");
			tmp->sep[0] = '\0';
			words -= 2;
		}
		else if((strncmp(word[0].word, "United",  256) == 0) &&
		        (strncmp(word[1].word, "States",  256) == 0) &&
		        ((strncmp(word[2].word, "Highway", 256) == 0) ||
		         (strncmp(word[2].word, "Hwy",     256) == 0)))
		{
			// e.g. United States Highway 6
			snprintf(word[0].word,  256, "%s", "US");
			snprintf(word[0].abrev, 256, "%s", "US");
			snprintf(word[0].sep,   256, "%s", word[2].sep);
			word[0].abreviate = 0;
			words -= 2;

			int i;
			for(i = 1; i < words; ++i)
			{
				snprintf(word[i].word,  256, "%s", word[i + 2].word);
				snprintf(word[i].abrev, 256, "%s", word[i + 2].abrev);
				snprintf(word[i].sep,   256, "%s", word[i + 2].sep);
				word[i].abreviate = word[i + 2].abreviate;
			}
		}
	}

	if(words >= 2)
	{
		if(strncmp(word[words - 1].word, "ft", 256) == 0)
		{
			// trim elevation from name
			// e.g. "Mt Meeker 13,870 ft"
			// LOGW("trim %s", input);
			words -= 2;
		}
		else if((strncmp(word[words - 2].word, "Multiuse", 256) == 0) &&
		        (strncmp(word[words - 1].word, "Path",     256) == 0))
		{
			// abreviate Multiuse Path
			osm_token_t* tmp = &word[words - 2];
			snprintf(tmp->word,  256, "%s", "MUP");
			snprintf(tmp->abrev, 256, "%s", "MUP");
			tmp->sep[0] = '\0';
			words -= 1;
		}
		else if((strncmp(word[0].word, "State",  256) == 0) &&
		        ((strncmp(word[1].word, "Highway", 256) == 0) ||
		         (strncmp(word[1].word, "Hwy",     256) == 0)))
		{
			// e.g. State Highway 93
			snprintf(word[0].word,  256, "%s", "Hwy");
			snprintf(word[0].abrev, 256, "%s", "Hwy");
			snprintf(word[0].sep,   256, "%s", word[1].sep);
			word[0].abreviate = 0;
			words -= 1;

			int i;
			for(i = 1; i < words; ++i)
			{
				snprintf(word[i].word,  256, "%s", word[i + 1].word);
				snprintf(word[i].abrev, 256, "%s", word[i + 1].abrev);
				snprintf(word[i].sep,   256, "%s", word[i + 1].sep);
				word[i].abreviate = word[i + 1].abreviate;
			}

			// prefer ref (if exists) for state highways
			// e.g. State Highway 72 => CO 72
			self->tag_highway = 1;
		}
		else if((strncmp(word[0].word, "State", 256) == 0) &&
		        ((strncmp(word[1].word, "Route",   256) == 0) ||
		         (strncmp(word[1].word, "Rte",     256) == 0)))
		{
			// e.g. State Rte XX
			snprintf(word[0].word,  256, "%s", "Rte");
			snprintf(word[0].abrev, 256, "%s", "Rte");
			snprintf(word[0].sep,   256, "%s", word[1].sep);
			word[0].abreviate = 0;
			words -= 1;

			int i;
			for(i = 1; i < words; ++i)
			{
				snprintf(word[i].word,  256, "%s", word[i + 1].word);
				snprintf(word[i].abrev, 256, "%s", word[i + 1].abrev);
				snprintf(word[i].sep,   256, "%s", word[i + 1].sep);
				word[i].abreviate = word[i + 1].abreviate;
			}

			// prefer ref (if exists) for state routes
			// e.g. State Rte XX => CO XX
			self->tag_highway = 1;
		}
		else if((strncmp(word[words - 2].word, "Trail", 256) == 0) &&
		        (strncmp(word[words - 1].word, "Head",  256) == 0))
		{
			// abreviate Trail Head (incorrect spelling)
			osm_token_t* tmp = &word[words - 2];
			snprintf(tmp->word,  256, "%s", "TH");
			snprintf(tmp->abrev, 256, "%s", "TH");
			tmp->sep[0] = '\0';
			tmp->sep[1] = '\0';
			words -= 1;
		}
		else if((strncmp(word[0].word, "County",   256) == 0) &&
		        ((strncmp(word[1].word, "Road",    256) == 0) ||
		         (strncmp(word[1].word, "Rd",      256) == 0) ||
		         (strncmp(word[1].word, "Highway", 256) == 0) ||
		         (strncmp(word[1].word, "Hwy",     256) == 0)))
		{
			// e.g. County Road 11D
			snprintf(word[0].word,  256, "%s", "CR");
			snprintf(word[0].abrev, 256, "%s", "CR");
			snprintf(word[0].sep,   256, "%s", word[1].sep);
			word[0].abreviate = 0;
			words -= 1;

			int i;
			for(i = 1; i < words; ++i)
			{
				snprintf(word[i].word,  256, "%s", word[i + 1].word);
				snprintf(word[i].abrev, 256, "%s", word[i + 1].abrev);
				snprintf(word[i].sep,   256, "%s", word[i + 1].sep);
				word[i].abreviate = word[i + 1].abreviate;
			}
		}
		else if((strncmp(word[0].word, "US",      256) == 0) &&
		        ((strncmp(word[1].word, "Highway", 256) == 0) ||
		         (strncmp(word[1].word, "Hwy",     256) == 0)))
		{
			// e.g. US Highway 6
			snprintf(word[0].word,  256, "%s", "US");
			snprintf(word[0].abrev, 256, "%s", "US");
			snprintf(word[0].sep,   256, "%s", word[1].sep);
			word[0].abreviate = 0;
			words -= 1;

			int i;
			for(i = 1; i < words; ++i)
			{
				snprintf(word[i].word,  256, "%s", word[i + 1].word);
				snprintf(word[i].abrev, 256, "%s", word[i + 1].abrev);
				snprintf(word[i].sep,   256, "%s", word[i + 1].sep);
				word[i].abreviate = word[i + 1].abreviate;
			}
		}
	}

	if(words == 0)
	{
		// input is null string
		// LOGW("invalid line=%i, name=%s", line, input);
		return 0;
	}
	else if(words == 1)
	{
		if(((strncmp(word[0].word, "Highway", 256) == 0) ||
		    (strncmp(word[0].word, "Hwy",     256) == 0)))
		{
			// prefer ref for state highways
			// e.g. Highway 119 => CO 119
			self->tag_highway = 1;
		}

		// input is single word (don't abreviate)
		snprintf(name, 256, "%s%s", word[0].word, word[0].sep);
		return 1;
	}
	else if(words == 2)
	{
		osm_catWord(name, word[0].word);
		osm_catWord(name, word[0].sep);
		osm_catWord(name, word[1].word);
		osm_catWord(name, word[1].sep);

		// input is two words
		if(word[1].abreviate)
		{
			// don't abreviate first word if second
			// word is also abrev
			osm_catWord(abrev, word[0].word);
			osm_catWord(abrev, word[0].sep);
			osm_catWord(abrev, word[1].abrev);
			osm_catWord(abrev, word[1].sep);
		}
		else if(word[0].abreviate)
		{
			osm_catWord(abrev, word[0].abrev);
			osm_catWord(abrev, word[0].sep);
			osm_catWord(abrev, word[1].word);
			osm_catWord(abrev, word[1].sep);
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
	osm_catWord(name, word[1].sep);
	if(word[1].abreviate)
	{
		abreviate = 1;
		osm_catWord(abrev, word[1].abrev);
	}
	else
	{
		osm_catWord(abrev, word[1].word);
	}
	osm_catWord(abrev, word[1].sep);

	// parse the rest of the line
	int n = 2;
	while(n < words)
	{
		osm_catWord(name, word[n].word);
		osm_catWord(name, word[n].sep);

		if(word[n].abreviate)
		{
			abreviate = 1;
			osm_catWord(abrev, word[n].abrev);
		}
		else
		{
			osm_catWord(abrev, word[n].word);
		}
		osm_catWord(abrev, word[n].sep);

		++n;
	}

	// clear abrev when no words abreviated
	if(abreviate == 0)
	{
		abrev[0] = '\0';
	}

	return 1;
}

static int
osm_parser_parseEle(osm_parser_t* self,
                    int line, const char* a, int ft)
{
	ASSERT(self);
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
	str = osm_parser_parseWord(self, 1, line, str, &w0);
	if(str == NULL)
	{
		// input is null string
		// LOGW("invalid line=%i, ele=%s", line, a);
		return 0;
	}

	str = osm_parser_parseWord(self, 0, line, str, &w1);
	if(str == NULL)
	{
		// input is single word
		return (int) (ele + 0.5f);
	}

	str = osm_parser_parseWord(self, 0, line, str, &wn);
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

	self->name_en            = 0;
	self->protect_class      = 0;
	self->ownership_national = 1;
	self->tag_name[0]        = '\0';
	self->tag_abrev[0]       = '\0';
	self->tag_ref[0]         = '\0';
	self->tag_highway        = 0;
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

	self->name_en            = 0;
	self->protect_class      = 0;
	self->ownership_national = 1;
	self->tag_name[0]        = '\0';
	self->tag_abrev[0]       = '\0';
	self->tag_ref[0]         = '\0';
	self->tag_highway        = 0;
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

	self->name_en            = 0;
	self->protect_class      = 0;
	self->ownership_national = 1;
	self->tag_name[0]        = '\0';
	self->tag_abrev[0]       = '\0';
	self->tag_ref[0]         = '\0';
	self->tag_highway        = 0;
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
	int i          = 0;
	int zoom[]     = { 15, 12, 9, 6, -1 };
	int max_zoom[] = { 1000, 15, 12, 9, -1 };
	int pow2n[]    = { 32768, 4096, 512, 64 };

	int type_array[]  =
	{
		OSMDB_TYPE_TILEREF_NODE15,
		OSMDB_TYPE_TILEREF_NODE12,
		OSMDB_TYPE_TILEREF_NODE9,
		OSMDB_TYPE_TILEREF_NODE6,
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

	// override custom classes
	if(self->ownership_national &&
	   ((self->node_info->class == self->boundary_np) ||
	    (self->node_info->class == self->boundary_pa)))
	{
		if(self->protect_class == 2)
		{
			self->node_info->class = self->boundary_np2;
		}
		else if(self->protect_class == 3)
		{
			self->node_info->class = self->boundary_nm3;
		}
	}

	const char* class_name;
	class_name = osmdb_classCodeToName(self->node_info->class);

	// select nodes when a point and name exists
	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style, class_name);
	if((sc == NULL) || (sc->point == NULL))
	{
		int is_bldg = self->node_info->flags &
		              OSMDB_NODEINFO_FLAG_BUILDING;
		if(is_bldg)
		{
			sc = osmdb_style_class(self->style, "building:yes");
		}
	}

	int has_name = 0;
	if((self->tag_name[0] != '\0') ||
	   ((self->node_info->class == self->highway_junction) &&
	    (self->tag_ref[0] != '\0')))
	{
		has_name = 1;
	}

	if(sc && sc->point && has_name)
	{
		int min_zoom = sc->point->min_zoom;

		// fill the name
		if((self->node_info->class == self->highway_junction) &&
	       (self->tag_ref[0] != '\0'))
		{
			self->node_info->flags |= OSMDB_NODEINFO_FLAG_NAMEREF;
			osmdb_nodeInfo_addName(self->node_info,
			                       self->tag_ref);
		}
		else if((self->tag_abrev[0] == '\0') || (sc->abrev == 0))
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

static void
osm_parser_truncate(char* s, char c)
{
	ASSERT(s);

	// truncate string to first instance of c
	int i = 0;
	while(s[i] != '\0')
	{
		if(s[i] == c)
		{
			s[i] = '\0';
			break;
		}

		++i;
	}
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

			// set the building flag
			if(strcmp(atts[j], "building") == 0)
			{
				self->node_info->flags |= OSMDB_NODEINFO_FLAG_BUILDING;
			}

			char name[256];
			char abrev[256];
			int  class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				if((class == self->boundary_np) ||
				   (class == self->boundary_pa))
				{
					// overwrite any class with national park
					self->node_info->class = class;
				}
				else if((self->node_info->class == self->boundary_np) ||
				        (self->node_info->class == self->boundary_pa))
				{
					// keep national park class
				}
				else if((self->node_info->class == self->class_none)   ||
				        (self->node_info->class == self->building_yes) ||
				        (self->node_info->class == self->barrier_yes)  ||
				        (self->node_info->class == self->office_yes)   ||
				        (self->node_info->class == self->historic_yes) ||
				        (self->node_info->class == self->man_made_yes) ||
				        (self->node_info->class == self->tourism_yes)  ||
				        osmdb_classIsBuilding(self->node_info->class))
				{
					// overwrite generic class
					self->node_info->class = class;
				}
			}
			else if(strcmp(atts[j], "name") == 0)
			{
				osm_parser_truncate(val, ';');
				if((self->name_en == 0) &&
			        osm_parser_parseName(self, line, val,
			                             name, abrev))
				{
					snprintf(self->tag_name,  256, "%s", name);
					snprintf(self->tag_abrev, 256, "%s", abrev);
				}
			}
			else if(strcmp(atts[j], "name:en") == 0)
			{
				osm_parser_truncate(val, ';');
				if(osm_parser_parseName(self, line, val, name, abrev))
				{
					self->name_en = 1;
					snprintf(self->tag_name,  256, "%s", name);
					snprintf(self->tag_abrev, 256, "%s", abrev);
				}
			}
			else if((strcmp(atts[j], "ref") == 0) ||
			        ((strcmp(atts[j], "junction:ref") == 0) &&
			         (self->tag_ref[0] == '\0')))
			{
				osm_parser_truncate(val, ';');
				snprintf(self->tag_ref,  256, "%s", val);
			}
			else if(strcmp(atts[j], "capital") == 0)
			{
				if(strcmp(val, "yes") == 0)
				{
					self->node_info->flags |= OSMDB_NODEINFO_FLAG_COUNTRY_CAPITAL;
				}
				if(strcmp(val, "4") == 0)
				{
					self->node_info->flags |= OSMDB_NODEINFO_FLAG_STATE_CAPITAL;
				}
			}
			else if(strcmp(atts[j], "state_capital") == 0)
			{
				if(strcmp(val, "yes") == 0)
				{
					self->node_info->flags |= OSMDB_NODEINFO_FLAG_STATE_CAPITAL;
				}
			}
			else if(strcmp(atts[j], "ele:ft") == 0)
			{
				self->node_info->ele = osm_parser_parseEle(self, line,
				                                           val, 1);
			}
			else if(strcmp(atts[j], "ele") == 0)
			{
				self->node_info->ele = osm_parser_parseEle(self, line,
				                                           val, 0);
			}
			else if((strcmp(atts[j], "protect_id") == 0) ||
			        (strcmp(atts[j], "protect_class") == 0))
			{
				// note that 1a,1b are possible but we don't use those
				self->protect_class = (int) strtol(val, NULL, 0);
			}
			else if(strcmp(atts[j], "ownership") == 0)
			{
				if(strcmp(val, "national") != 0)
				{
					self->ownership_national = 0;
				}
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
		OSMDB_TYPE_TILEREF_WAY15,
		OSMDB_TYPE_TILEREF_WAY12,
		OSMDB_TYPE_TILEREF_WAY9,
		OSMDB_TYPE_TILEREF_WAY6,
	};
	int type_rel[]  =
	{
		OSMDB_TYPE_TILEREF_REL15,
		OSMDB_TYPE_TILEREF_REL12,
		OSMDB_TYPE_TILEREF_REL9,
		OSMDB_TYPE_TILEREF_REL6,
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
	int   i          = 0;
	int   zoom[]     = { 15, 12, 9, 6, -1 };
	int   max_zoom[] = { 1000, 15, 12, 9, -1 };
	int   pow2n[]    = { 32768, 4096, 512, 64 };
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

static int min(int a, int b)
{
	return (b < a) ? b : a;
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

	// override custom classes
	if(self->ownership_national &&
	   ((self->way_info->class == self->boundary_np) ||
	    (self->way_info->class == self->boundary_pa)))
	{
		if(self->protect_class == 2)
		{
			self->way_info->class = self->boundary_np2;
		}
		else if(self->protect_class == 3)
		{
			self->way_info->class = self->boundary_nm3;
		}
	}

	const char* class_name;
	class_name = osmdb_classCodeToName(self->way_info->class);

	// select ways
	osmdb_styleClass_t* sc1;
	osmdb_styleClass_t* sc2 = NULL;
	sc1 = osmdb_style_class(self->style, class_name);

	int is_bldg = self->way_info->flags &
	              OSMDB_WAYINFO_FLAG_BUILDING;
	if(is_bldg)
	{
		sc2 = osmdb_style_class(self->style, "building:yes");
	}

	int min_zoom = 999;
	if(sc1 || sc2)
	{
		int has_name = 0;
		if((self->tag_name[0] != '\0') ||
		   (self->tag_ref[0]  != '\0'))
		{
			has_name = 1;
		}

		// select the way as a line
		// when named or when the named mode is not set
		if(sc1 &&
		   (sc1->line &&
		    (has_name ||
		     ((sc1->line->mode & OSMDB_STYLE_MODE_NAMED) == 0))))
		{
			selected = 1;
			min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc1));
		}
		else if(sc2 &&
		        (sc2->line &&
		         (has_name ||
		          ((sc2->line->mode & OSMDB_STYLE_MODE_NAMED) == 0))))
		{
			selected = 1;
			min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc2));
		}

		// select the way as a polygon
		if(sc1 && sc1->poly)
		{
			polygon  = 1;
			selected = 1;
			min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc1));
		}
		else if(sc2 && sc2->poly)
		{
			polygon  = 1;
			selected = 1;
			min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc2));
		}

		// select the way as a point when named
		if(sc1 && sc1->point && has_name)
		{
			// set the center flag when not selected as a line/poly
			if(selected == 0)
			{
				center = 1;
			}

			selected = 1;
			min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc1));
		}
		else if(sc2 && sc2->point && has_name)
		{
			// set the center flag when not selected as a line/poly
			if(selected == 0)
			{
				center = 1;
			}

			selected = 1;
			min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc2));
		}
	}

	// fill the name
	if((self->way_info->class == self->highway_motorway) &&
	   (self->tag_ref[0] != '\0'))
	{
		// prefer ref for motorways
		self->way_info->flags |= OSMDB_WAYINFO_FLAG_NAMEREF;
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_ref);
	}
	else if(self->tag_highway && (self->tag_ref[0] != '\0'))
	{
		// prefer ref for highways
		// e.g. State Highway 72 or Highway 119
		self->way_info->flags |= OSMDB_WAYINFO_FLAG_NAMEREF;
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_ref);
	}
	else if((self->tag_abrev[0] != '\0') &&
		    sc1 && sc1->abrev)
	{
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_abrev);
	}
	else if(self->tag_name[0] != '\0')
	{
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_name);
	}
	else if(self->tag_ref[0] != '\0')
	{
		self->way_info->flags |= OSMDB_WAYINFO_FLAG_NAMEREF;
		osmdb_wayInfo_addName(self->way_info,
		                      self->tag_ref);
	}

	// always add ways since they may be transitively selected
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

			// set the building flag
			if(strcmp(atts[j], "building") == 0)
			{
				self->way_info->flags |= OSMDB_WAYINFO_FLAG_BUILDING;
			}

			char name[256];
			char abrev[256];
			int  class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				if((class == self->boundary_np) ||
				   (class == self->boundary_pa))
				{
					// overwrite any class with national park
					self->way_info->class = class;
				}
				else if((self->way_info->class == self->boundary_np) ||
				        (self->way_info->class == self->boundary_pa))
				{
					// keep national park class
				}
				else if((self->way_info->class == self->class_none)   ||
				        (self->way_info->class == self->building_yes) ||
				        (self->way_info->class == self->barrier_yes)  ||
				        (self->way_info->class == self->office_yes)   ||
				        (self->way_info->class == self->historic_yes) ||
				        (self->way_info->class == self->man_made_yes) ||
				        (self->way_info->class == self->tourism_yes)  ||
				        osmdb_classIsBuilding(self->way_info->class))
				{
					// overwrite generic class
					self->way_info->class = class;
				}
			}
			else if(strcmp(atts[j], "name") == 0)
			{
				osm_parser_truncate(val, ';');
				if((self->name_en == 0) &&
			        osm_parser_parseName(self, line, val,
			                             name, abrev))
				{
					snprintf(self->tag_name,  256, "%s", name);
					snprintf(self->tag_abrev, 256, "%s", abrev);
				}
			}
			else if(strcmp(atts[j], "name:en") == 0)
			{
				osm_parser_truncate(val, ';');
				if(osm_parser_parseName(self, line, val, name, abrev))
				{
					self->name_en = 1;
					snprintf(self->tag_name,  256, "%s", name);
					snprintf(self->tag_abrev, 256, "%s", abrev);
				}
			}
			else if((strcmp(atts[j], "ref") == 0) ||
			        ((strcmp(atts[j], "junction:ref") == 0) &&
			         (self->tag_ref[0] == '\0')))
			{
				osm_parser_truncate(val, ';');
				snprintf(self->tag_ref,  256, "%s", val);
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
			else if((strcmp(atts[j], "protect_id") == 0) ||
			        (strcmp(atts[j], "protect_class") == 0))
			{
				// note that 1a,1b are possible but we don't use those
				self->protect_class = (int) strtol(val, NULL, 0);
			}
			else if(strcmp(atts[j], "ownership") == 0)
			{
				if(strcmp(val, "national") != 0)
				{
					self->ownership_national = 0;
				}
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
		                   data[i].wid, &hnd_way_range) == 0)
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
			way_range->wid  = data[i].wid;
			way_range->latT = 0.0;
			way_range->lonL = 0.0;
			way_range->latB = 0.0;
			way_range->lonR = 0.0;

			if(osmdb_index_get(self->index, 0,
			                   OSMDB_TYPE_WAYNDS,
			                   data[i].wid, &hnd_way_nds) == 0)
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
	// the size of large areas was determined experimentally
	// 0.002 is roughly the size of 16 z15 tiles
	// or the size of Antero Reservoir
	double latT = self->rel_range->latT;
	double lonL = self->rel_range->lonL;
	double latB = self->rel_range->latB;
	double lonR = self->rel_range->lonR;
	float  area = (float) ((latT-latB)*(lonR-lonL));
	if((center == 0) &&
	   ((polygon == 0) ||
	    (polygon && (area < 0.002f))))
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

	// override custom classes
	if(self->ownership_national &&
	   ((self->rel_info->class == self->boundary_np) ||
	    (self->rel_info->class == self->boundary_pa)))
	{
		if(self->protect_class == 2)
		{
			self->rel_info->class = self->boundary_np2;
		}
		else if(self->protect_class == 3)
		{
			self->rel_info->class = self->boundary_nm3;
		}
	}

	const char* class_name;
	class_name = osmdb_classCodeToName(self->rel_info->class);

	// select relations when a line/poly exists or
	// when a point and name exists
	osmdb_styleClass_t* sc1;
	osmdb_styleClass_t* sc2 = NULL;
	sc1 = osmdb_style_class(self->style, class_name);

	int is_bldg = self->rel_info->flags &
	              OSMDB_RELINFO_FLAG_BUILDING;
	if(is_bldg)
	{
		sc2 = osmdb_style_class(self->style, "building:yes");
	}

	int min_zoom = 999;
	if(sc1 && (sc1->line || sc1->poly))
	{
		if(sc1->poly)
		{
			polygon = 1;
		}

		selected = 1;
		min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc1));
	}
	else if(sc2 && (sc2->line || sc2->poly))
	{
		if(sc2->poly)
		{
			polygon = 1;
		}

		selected = 1;
		min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc2));
	}
	else if(sc1 && sc1->point && (self->tag_name[0] != '\0'))
	{
		selected = 1;
		center   = 1;
		min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc1));
	}
	else if(sc2 && sc2->point && (self->tag_name[0] != '\0'))
	{
		selected = 1;
		center   = 1;
		min_zoom = min(min_zoom, osmdb_styleClass_minZoom(sc2));
	}

	// discard relations when not selected
	// or if the type is not supported
	if((selected == 0) ||
	   (self->rel_info->type == OSMDB_RELINFO_TYPE_NONE))
	{
		return 1;
	}

	// fill the name
	if((self->tag_abrev[0] == '\0') ||
	   (sc1 && (sc1->abrev == 0)))
	{
		osmdb_relInfo_addName(self->rel_info,
		                      self->tag_name);
	}
	else
	{
		osmdb_relInfo_addName(self->rel_info,
		                      self->tag_abrev);
	}

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

			// set the building flag
			if(strcmp(atts[j], "building") == 0)
			{
				self->rel_info->flags |= OSMDB_RELINFO_FLAG_BUILDING;
			}

			char name[256];
			char abrev[256];
			int  class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				if((class == self->boundary_np) ||
				   (class == self->boundary_pa))
				{
					// overwrite any class with national park
					self->rel_info->class = class;
				}
				else if((self->rel_info->class == self->boundary_np) ||
				        (self->rel_info->class == self->boundary_pa))
				{
					// keep national park class
				}
				else if((self->rel_info->class == self->class_none)   ||
				        (self->rel_info->class == self->building_yes) ||
				        (self->rel_info->class == self->barrier_yes)  ||
				        (self->rel_info->class == self->office_yes)   ||
				        (self->rel_info->class == self->historic_yes) ||
				        (self->rel_info->class == self->man_made_yes) ||
				        (self->rel_info->class == self->tourism_yes)  ||
				        osmdb_classIsBuilding(self->rel_info->class))
				{
					// overwrite generic class
					self->rel_info->class = class;
				}
			}
			else if(strcmp(atts[j], "name") == 0)
			{
				osm_parser_truncate(val, ';');
				if((self->name_en == 0) &&
			        osm_parser_parseName(self, line, val,
			                             name, abrev))
				{
					snprintf(self->tag_name,  256, "%s", name);
					snprintf(self->tag_abrev, 256, "%s", abrev);
				}
			}
			else if(strcmp(atts[j], "name:en") == 0)
			{
				osm_parser_truncate(val, ';');
				if(osm_parser_parseName(self, line, val, name, abrev))
				{
					self->name_en = 1;
					snprintf(self->tag_name,  256, "%s", name);
					snprintf(self->tag_abrev, 256, "%s", abrev);
				}
			}
			else if((strcmp(atts[j], "ref") == 0) ||
			        ((strcmp(atts[j], "junction:ref") == 0) &&
			         (self->tag_ref[0] == '\0')))
			{
				osm_parser_truncate(val, ';');
				snprintf(self->tag_ref,  256, "%s", val);
			}
			else if((strcmp(atts[j], "type") == 0))
			{
				self->rel_info->type = osmdb_relationTagTypeToCode(val);
			}
			else if((strcmp(atts[j], "protect_id") == 0) ||
			        (strcmp(atts[j], "protect_class") == 0))
			{
				// note that 1a,1b are possible but we don't use those
				self->protect_class = (int) strtol(val, NULL, 0);
			}
			else if(strcmp(atts[j], "ownership") == 0)
			{
				if(strcmp(val, "national") != 0)
				{
					self->ownership_national = 0;
				}
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

	// initialize data
	memset((void*) data, 0, sizeof(osmdb_relData_t));

	// parse the member
	int     i    = 0;
	int     j    = 1;
	int     type = 0;
	int     role = 0;
	int64_t ref  = 0;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "ref")  == 0)
		{
			ref = (int64_t) strtoll(atts[j], NULL, 0);
		}
		else if(strcmp(atts[i], "type")  == 0)
		{
			type = osmdb_relationMemberTypeToCode(atts[j]);
		}
		else if(strcmp(atts[i], "role")  == 0)
		{
			role = osmdb_relationMemberRoleToCode(atts[j]);
		}

		i += 2;
		j += 2;
	}

	// store the admin_centre or way member
	// ignore unsupported member types
	if((type == self->rel_member_type_node) &&
	   ((role == self->rel_member_role_admin_centre) ||
	    (role == self->rel_member_role_label)))
	{
		self->rel_info->nid = ref;
	}
	else if(type == self->rel_member_type_way)
	{
		data->wid = ref;
		if(role == self->rel_member_role_inner)
		{
			data->inner = 1;
		}
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

	if(bfs_util_initialize() == 0)
	{
		goto fail_init;
	}

	self->index = osmdb_index_new(db_name,
	                              OSMDB_INDEX_MODE_CREATE,
	                              1, 4.0f);
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

	self->nocaps_map = cc_map_new();
	if(self->nocaps_map == NULL)
	{
		goto fail_nocaps_map;
	}

	if(osm_parser_fillNocaps(self) == 0)
	{
		goto fail_fill_nocaps;
	}

	self->abrev_map = cc_map_new();
	if(self->abrev_map == NULL)
	{
		goto fail_abrev_map;
	}

	if(osm_parser_fillAbrev(self) == 0)
	{
		goto fail_fill_abrev;
	}

	// initialize locale for iconv
	setlocale(LC_CTYPE, "");

	self->cd = iconv_open("ASCII//TRANSLIT", "UTF-8");
	if(self->cd == ICONV_OPEN_ERR)
	{
		LOGE("iconv_open failed");
		goto fail_iconv_open;
	}

	self->class_none       = osmdb_classKVToCode("class",    "none");
	self->building_yes     = osmdb_classKVToCode("building", "yes");
	self->barrier_yes      = osmdb_classKVToCode("barrier",  "yes");
	self->office_yes       = osmdb_classKVToCode("office",   "yes");
	self->historic_yes     = osmdb_classKVToCode("historic", "yes");
	self->man_made_yes     = osmdb_classKVToCode("man_made", "yes");
	self->tourism_yes      = osmdb_classKVToCode("tourism",  "yes");
	self->highway_motorway = osmdb_classKVToCode("highway",  "motorway");
	self->highway_junction = osmdb_classKVToCode("highway",  "motorway_junction");
	self->boundary_np      = osmdb_classKVToCode("boundary", "national_park");
	self->boundary_np2     = osmdb_classKVToCode("boundary", "national_park2");
	self->boundary_nm3     = osmdb_classKVToCode("boundary", "national_monument3");
	self->boundary_pa      = osmdb_classKVToCode("boundary", "protected_area");

	self->rel_member_type_node         = osmdb_relationMemberTypeToCode("node");
	self->rel_member_type_way          = osmdb_relationMemberTypeToCode("way");
	self->rel_member_role_inner        = osmdb_relationMemberRoleToCode("inner");
	self->rel_member_role_admin_centre = osmdb_relationMemberRoleToCode("admin_centre");
	self->rel_member_role_label        = osmdb_relationMemberRoleToCode("label");

	// success
	return self;

	// failure
	fail_iconv_open:
		cc_map_discard(self->abrev_map);
	fail_fill_abrev:
		cc_map_delete(&self->abrev_map);
	fail_abrev_map:
		cc_map_discard(self->nocaps_map);
	fail_fill_nocaps:
		cc_map_delete(&self->nocaps_map);
	fail_nocaps_map:
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
		bfs_util_shutdown();
	fail_init:
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

		cc_map_discard(self->abrev_map);
		cc_map_delete(&self->abrev_map);

		cc_map_discard(self->nocaps_map);
		cc_map_delete(&self->nocaps_map);

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
		bfs_util_shutdown();

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

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

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <iconv.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>


#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "libcc/cc_memory.h"
#include "libcc/cc_timestamp.h"
#include "libxmlstream/xml_istream.h"
#include "terrain/terrain_util.h"
#include "osm_parser.h"
#include "../osmdb_page.h"
#include "../osmdb_util.h"

#define OSM_PARSER_CACHE_SIZE 4000000000

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

#define OSM_BATCH_SIZE_MAX 10000

#define ICONV_OPEN_ERR ((iconv_t) (-1))
#define ICONV_CONV_ERR ((size_t) (-1))

/***********************************************************
* private - class utils                                    *
***********************************************************/

static int osm_parser_logProgress(osm_parser_t* self)
{
	ASSERT(self);

	double t2 = cc_timestamp();
	double dt = t2 - self->t1;
	if(dt >= 10.0)
	{
		self->t1 = t2;
		return 1;
	}

	return 0;
}

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
              char* abrev, char* text)
{
	ASSERT(input);
	ASSERT(name);
	ASSERT(abrev);
	ASSERT(text);

	// initialize output string
	name[0]  = '\0';
	abrev[0] = '\0';
	text[0]  = '\0';

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

	// parse the search text
	int n = 0;
	for(n = 0; n < words; ++n)
	{
		if(n > 0)
		{
			osm_catWord(text, " ");
		}

		osm_catWord(text, word[n].word);
		if(word[n].abreviate)
		{
			osm_catWord(text, " ");
			osm_catWord(text, word[n].abrev);
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
	n = 2;
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

static int osm_parseSt(const char* num)
{
	ASSERT(num);

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

static void osm_parser_init(osm_parser_t* self)
{
	ASSERT(self);

	self->attr_id         = 0.0;
	self->attr_lat        = 0.0;
	self->attr_lon        = 0.0;
	self->name_en         = 0;
	self->tag_name[0]     = '\0';
	self->tag_abrev[0]    = '\0';
	self->tag_text[0]     = '\0';
	self->tag_ele         = 0;
	self->tag_st          = 0;
	self->tag_class       = 0;
	self->tag_way_layer   = 0;
	self->tag_way_oneway  = 0;
	self->tag_way_bridge  = 0;
	self->tag_way_tunnel  = 0;
	self->tag_way_cutting = 0;
	self->ways_nds_idx    = 0;
}

static osmdb_page_t*
osm_parser_findPage(osm_parser_t* self, double id)
{
	ASSERT(self);

	// 4 bytes per tile
	off_t offset = 4*((off_t) id);
	off_t base   = OSMDB_PAGE_SIZE*(offset/OSMDB_PAGE_SIZE);

	// check for last page used
	osmdb_page_t*  page;
	cc_listIter_t* iter = cc_list_tail(self->page_list);
	if(iter)
	{
		page = (osmdb_page_t*) cc_list_peekIter(iter);
		if(page->base == base)
		{
			return page;
		}
	}

	// find page in cache
	cc_mapIter_t   miterator;
	cc_mapIter_t*  miter = &miterator;
	iter = (cc_listIter_t*)
	       cc_map_findf(self->page_map, miter, "%0.0lf",
	                    (double) base);
	if(iter)
	{
		page = (osmdb_page_t*) cc_list_peekIter(iter);
		cc_list_moven(self->page_list, iter, NULL);
		return page;
	}

	// get a new page
	page = osmdb_table_get(self->page_table, base);
	if(page == NULL)
	{
		return NULL;
	}

	// trim list
	iter = cc_list_head(self->page_list);
	while(iter)
	{
		size_t size = MEMSIZE();
		if(size < OSM_PARSER_CACHE_SIZE)
		{
			break;
		}

		osmdb_page_t* tmp_page;
		tmp_page = (osmdb_page_t*)
		           cc_list_peekIter(iter);

		miter = &miterator;
		cc_map_findf(self->page_map, miter, "%0.0lf",
		             (double) tmp_page->base);
		cc_list_remove(self->page_list, &iter);
		cc_map_remove(self->page_map, &miter);
		if(osmdb_table_put(self->page_table, &tmp_page) == 0)
		{
			goto fail_trim;
		}
	}

	// add page to list
	iter = cc_list_append(self->page_list, NULL,
	                      (const void*) page);
	if(iter == NULL)
	{
		goto fail_append;
	}

	// add page to map
	if(cc_map_addf(self->page_map, (const void*) iter,
	               "%0.0lf", (double) base) == 0)
	{
		goto fail_add;
	}

	// success
	return page;

	// failure
	fail_add:
		cc_list_remove(self->page_list, &iter);
	fail_append:
	fail_trim:
		osmdb_table_put(self->page_table, &page);
	return NULL;
}

static int
osm_parser_getTile(osm_parser_t* self,
                   double id, unsigned short* tile)
{
	ASSERT(self);
	ASSERT(tile);

	osmdb_page_t* page;
	page = osm_parser_findPage(self, id);
	if(page == NULL)
	{
		return 0;
	}

	osmdb_page_get(page, id, tile);

	return 1;
}

static int
osm_parser_setTile(osm_parser_t* self,
                   double id, unsigned short* tile)
{
	ASSERT(self);
	ASSERT(tile);

	osmdb_page_t* page;
	page = osm_parser_findPage(self, id);
	if(page == NULL)
	{
		return 0;
	}

	osmdb_page_set(page, id, tile);

	return 1;
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

	return 1;
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
osm_parser_insertNodesInfo(osm_parser_t* self, int min_zoom)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_nid      = self->idx_insert_nodes_info_nid;
	int idx_class    = self->idx_insert_nodes_info_class;
	int idx_name     = self->idx_insert_nodes_info_name;
	int idx_abrev    = self->idx_insert_nodes_info_abrev;
	int idx_ele      = self->idx_insert_nodes_info_ele;
	int idx_st       = self->idx_insert_nodes_info_st;
	int idx_min_zoom = self->idx_insert_nodes_info_min_zoom;
	int bytes_name   = strlen(self->tag_name)  + 1;
	int bytes_abrev  = strlen(self->tag_abrev) + 1;

	sqlite3_stmt* stmt = self->stmt_insert_nodes_info;
	if((sqlite3_bind_double(stmt, idx_nid, self->attr_id) != SQLITE_OK)  ||
	   (sqlite3_bind_int(stmt, idx_class, self->tag_class) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_name, self->tag_name,
	                      bytes_name, SQLITE_STATIC) != SQLITE_OK)       ||
	   (sqlite3_bind_text(stmt, idx_abrev, self->tag_abrev,
	                      bytes_abrev, SQLITE_STATIC) != SQLITE_OK)      ||
	   (sqlite3_bind_int(stmt, idx_ele, self->tag_ele) != SQLITE_OK)     ||
	   (sqlite3_bind_int(stmt, idx_st, self->tag_st) != SQLITE_OK)       ||
	   (sqlite3_bind_int(stmt, idx_min_zoom, min_zoom) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertNodesText(osm_parser_t* self)
{
	ASSERT(self);

	if(self->tag_text[0] == '\0')
	{
		// ignore
		return 1;
	}

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	char text[256];
	if(self->tag_st)
	{
		snprintf(text, 256, "%0.0lf|%s %s %s\n",
		         self->attr_id, self->tag_text,
		         osmdb_stCodeToName(self->tag_st),
		         osmdb_stCodeToAbrev(self->tag_st));
	}
	else
	{
		snprintf(text, 256, "%0.0lf|%s\n",
		         self->attr_id, self->tag_text);
	}

	int idx_nid    = self->idx_insert_nodes_text_nid;
	int idx_txt    = self->idx_insert_nodes_text_txt;
	int bytes_text = strlen(text) + 1;

	sqlite3_stmt* stmt = self->stmt_insert_nodes_text;
	if((sqlite3_bind_double(stmt, idx_nid,
	                        self->attr_id) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_txt, text,
	                      bytes_text, SQLITE_STATIC) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertNodesCoords(osm_parser_t* self)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_nid = self->idx_insert_nodes_coords_nid;
	int idx_lat = self->idx_insert_nodes_coords_lat;
	int idx_lon = self->idx_insert_nodes_coords_lon;

	sqlite3_stmt* stmt = self->stmt_insert_nodes_coords;
	if((sqlite3_bind_double(stmt, idx_nid,
	                        self->attr_id) != SQLITE_OK)  ||
	   (sqlite3_bind_double(stmt, idx_lat,
	                        self->attr_lat) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_lon,
	                        self->attr_lon) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertNodesRange(osm_parser_t* self,
                            unsigned short* tile)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_nid = self->idx_insert_nodes_range_nid;
	int idx_l   = self->idx_insert_nodes_range_l;
	int idx_r   = self->idx_insert_nodes_range_r;
	int idx_b   = self->idx_insert_nodes_range_b;
	int idx_t   = self->idx_insert_nodes_range_t;

	// adjust tile position to center since sqlite tweaks the
	// values outward slightly due to floating point precision
	double l = floor((double) tile[0]) + 0.5;
	double r = l;
	double b = floor((double) tile[1]) + 0.5;
	double t = b;
	sqlite3_stmt* stmt = self->stmt_insert_nodes_range;
	if((sqlite3_bind_double(stmt, idx_nid,
	                        self->attr_id) != SQLITE_OK)  ||
	   (sqlite3_bind_double(stmt, idx_l, l) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_r, r) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_b, b) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_t, t) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_endOsmNode(osm_parser_t* self, int line,
                      float progress, const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSM_STATE_OSM;

	float x;
	float y;
	terrain_coord2tile(self->attr_lat, self->attr_lon,
	                   16, &x, &y);

	unsigned short tile[2] =
	{
		(unsigned short) x,
		(unsigned short) y
	};

	// select nodes when a point and name exists
	osmdb_styleClass_t* sc;
	sc = osmdb_style_class(self->style,
	                       osmdb_classCodeToName(self->tag_class));
	if(sc && sc->point && (self->tag_name[0] != '\0'))
	{
		int min_zoom = sc->point->min_zoom;

		if((osm_parser_insertNodesText(self) == 0)  ||
		   (osm_parser_insertNodesRange(self, tile) == 0) ||
		   (osm_parser_insertNodesInfo(self, min_zoom) == 0))
		{
			return 0;
		}
	}

	// node tiles may be transitively selected
	if(osm_parser_setTile(self, self->attr_id, tile) == 0)
	{
		return 0;
	}

	if(osm_parser_insertNodesCoords(self) == 0)
	{
		return 0;
	}

	// update histogram
	++self->histogram[self->tag_class].nodes;
	self->stats_nodes += 1.0;
	if(osm_parser_logProgress(self))
	{
		double dt = self->t1 - self->t0;
		LOGI("dt=%0.0lf, line=%i, progress=%0.2f, nodes=%0.0lf",
		     dt, line, 100.0f*progress, self->stats_nodes);
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
			char text[256];

			int class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				// set or overwrite generic class
				if((self->tag_class == self->class_none)   ||
				   (self->tag_class == self->building_yes) ||
				   (self->tag_class == self->barrier_yes)  ||
				   (self->tag_class == self->office_yes)   ||
				   (self->tag_class == self->historic_yes) ||
				   (self->tag_class == self->man_made_yes) ||
				   (self->tag_class == self->tourism_yes))
				{
					self->tag_class = class;
				}
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        (self->name_en == 0) &&
			        osm_parseName(line, val, name, abrev, text))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
				snprintf(self->tag_text,  256, "%s", text);
			}
			else if((strcmp(atts[j], "name:en") == 0) &&
			        osm_parseName(line, val, name, abrev, text))
			{
				self->name_en = 1;
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
				snprintf(self->tag_text,  256, "%s", text);
			}
			else if(strcmp(atts[j], "ele:ft") == 0)
			{
				self->tag_ele = osm_parseEle(line, val, 1);
			}
			else if(strcmp(atts[j], "ele") == 0)
			{
				self->tag_ele = osm_parseEle(line, val, 0);
			}
			else if((strcmp(atts[j], "gnis:ST_num")   == 0) ||
			        (strcmp(atts[j], "gnis:state_id") == 0))
			{
				self->tag_st = osm_parseSt(val);
			}
			else if(strcmp(atts[j], "gnis:ST_alpha") == 0)
			{
				self->tag_st = osmdb_stAbrevToCode(val);
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
osm_parser_insertWays(osm_parser_t* self,
                      int center, int polygon,
                      int selected, int min_zoom)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_wid      = self->idx_insert_ways_wid;
	int idx_class    = self->idx_insert_ways_class;
	int idx_layer    = self->idx_insert_ways_layer;
	int idx_name     = self->idx_insert_ways_name;
	int idx_abrev    = self->idx_insert_ways_abrev;
	int idx_oneway   = self->idx_insert_ways_oneway;
	int idx_bridge   = self->idx_insert_ways_bridge;
	int idx_tunnel   = self->idx_insert_ways_tunnel;
	int idx_cutting  = self->idx_insert_ways_cutting;
	int idx_center   = self->idx_insert_ways_center;
	int idx_polygon  = self->idx_insert_ways_polygon;
	int idx_selected = self->idx_insert_ways_selected;
	int idx_min_zoom = self->idx_insert_ways_min_zoom;
	int idx_nds      = self->idx_insert_ways_nds;
	int bytes_name   = strlen(self->tag_name)  + 1;
	int bytes_abrev  = strlen(self->tag_abrev) + 1;

	const void* ways_nds_array = self->ways_nds_array;
	if(self->ways_nds_idx == 0)
	{
		ways_nds_array = NULL;
	}

	sqlite3_stmt* stmt = self->stmt_insert_ways;
	if((sqlite3_bind_double(stmt, idx_wid,
	                        self->attr_id) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_class,
	                        self->tag_class) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_layer,
	                        self->tag_way_layer) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_name, self->tag_name,
	                      bytes_name, SQLITE_STATIC) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_abrev, self->tag_abrev,
	                      bytes_abrev, SQLITE_STATIC) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_oneway,
	                     self->tag_way_oneway) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_bridge,
	                     self->tag_way_bridge) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_tunnel,
	                     self->tag_way_tunnel) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_cutting,
	                     self->tag_way_cutting) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_center, center) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_polygon, polygon) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_selected, selected) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_min_zoom, min_zoom) != SQLITE_OK) ||
	   (sqlite3_bind_blob(stmt, idx_nds, ways_nds_array,
	                      self->ways_nds_idx*sizeof(double),
	                      SQLITE_TRANSIENT) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertWaysText(osm_parser_t* self)
{
	ASSERT(self);

	if(self->tag_text[0] == '\0')
	{
		// ignore
		return 1;
	}

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_wid    = self->idx_insert_ways_text_wid;
	int idx_txt    = self->idx_insert_ways_text_txt;
	int bytes_text = strlen(self->tag_text) + 1;

	sqlite3_stmt* stmt = self->stmt_insert_ways_text;
	if((sqlite3_bind_double(stmt, idx_wid,
	                        self->attr_id) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_txt, self->tag_text,
	                      bytes_text, SQLITE_STATIC) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertWaysRange(osm_parser_t* self)
{
	ASSERT(self);

	double wid = self->attr_id;

	unsigned short l = 0;
	unsigned short r = 0;
	unsigned short b = 0;
	unsigned short t = 0;
	unsigned short tile[2];

	// compute range
	int i;
	for(i = 0; i < self->ways_nds_idx; ++i)
	{
		double nid = self->ways_nds_array[i];
		if(osm_parser_getTile(self, nid, tile) == 0)
		{
			return 0;
		}

		if(i == 0)
		{
			l = tile[0];
			r = tile[0];
			b = tile[1];
			t = tile[1];
			continue;
		}

		if(tile[0] < l)
		{
			l = tile[0];
		}

		if(tile[0] > r)
		{
			r = tile[0];
		}

		if(tile[1] < b)
		{
			b = tile[1];
		}

		if(tile[1] > t)
		{
			t = tile[1];
		}
	}

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_wid = self->idx_insert_ways_range_wid;
	int idx_l   = self->idx_insert_ways_range_l;
	int idx_r   = self->idx_insert_ways_range_r;
	int idx_b   = self->idx_insert_ways_range_b;
	int idx_t   = self->idx_insert_ways_range_t;

	// adjust tile position to center since sqlite tweaks the
	// values outward slightly due to floating point precision
	double dl = ((double) l) + 0.5;
	double dr = ((double) r) + 0.5;
	double db = ((double) b) + 0.5;
	double dt = ((double) t) + 0.5;
	sqlite3_stmt* stmt = self->stmt_insert_ways_range;
	if((sqlite3_bind_double(stmt, idx_wid,
	                        wid) != SQLITE_OK)             ||
	   (sqlite3_bind_double(stmt, idx_l, dl) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_r, dr) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_b, db) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_t, dt) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
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
	                       osmdb_classCodeToName(self->tag_class));
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

	// always add ways since they may be transitively selected
	int min_zoom = sc ? osmdb_styleClass_minZoom(sc) : 999;
	if(osm_parser_insertWays(self, center, polygon,
	                         selected, min_zoom) == 0)
	{
		return 0;
	}

	// add search text
	if(osm_parser_insertWaysText(self) == 0)
	{
		return 0;
	}

	if(osm_parser_insertWaysRange(self) == 0)
	{
		return 0;
	}

	// update histogram
	++self->histogram[self->tag_class].ways;
	self->stats_ways += 1.0;
	if(osm_parser_logProgress(self))
	{
		double dt = self->t1 - self->t0;
		LOGI("dt=%0.0lf, line=%i, progress=%0.2f, ways=%0.0lf",
		     dt, line, 100.0f*progress, self->stats_ways);
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
			char text[256];

			int class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				// set or overwrite generic class
				if((self->tag_class == self->class_none)   ||
				   (self->tag_class == self->building_yes) ||
				   (self->tag_class == self->barrier_yes)  ||
				   (self->tag_class == self->office_yes)   ||
				   (self->tag_class == self->historic_yes) ||
				   (self->tag_class == self->man_made_yes) ||
				   (self->tag_class == self->tourism_yes))
				{
					self->tag_class = class;
				}
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        (self->name_en == 0) &&
			        osm_parseName(line, val, name, abrev, text))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
				snprintf(self->tag_text,  256, "%s", text);
			}
			else if((strcmp(atts[j], "name:en") == 0) &&
			        osm_parseName(line, val, name, abrev, text))
			{
				self->name_en = 1;
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
				snprintf(self->tag_text,  256, "%s", text);
			}
			else if(strcmp(atts[j], "layer") == 0)
			{
				self->tag_way_layer = (int) strtol(val, NULL, 0);
			}
			else if(strcmp(atts[j], "oneway") == 0)
			{
				if(strcmp(val, "yes") == 0)
				{
					self->tag_way_oneway = 1;
				}
				else if(strcmp(val, "-1") == 0)
				{
					self->tag_way_oneway = -1;
				}
			}
			else if((strcmp(atts[j], "bridge") == 0) &&
				    (strcmp(val, "no") != 0))
			{
				self->tag_way_bridge = 1;
			}
			else if((strcmp(atts[j], "tunnel") == 0) &&
				    (strcmp(val, "no") != 0))
			{
				self->tag_way_tunnel = 1;
			}
			else if((strcmp(atts[j], "cutting") == 0) &&
				    (strcmp(val, "no") != 0))
			{
				self->tag_way_cutting = 1;
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

	// increase size of ways_nds_array
	if(self->ways_nds_idx == self->ways_nds_count)
	{
		int cnt = 2*self->ways_nds_count;

		double* tmp;
		tmp = REALLOC((void*) self->ways_nds_array,
		              cnt*sizeof(double));
		if(tmp == NULL)
		{
			LOGE("REALLOC failed");
			return 0;
		}
		self->ways_nds_count = cnt;
		self->ways_nds_array = tmp;
	}

	double ref = 0.0;

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "ref")  == 0)
		{
			ref = strtod(atts[j], NULL);
		}

		i += 2;
		j += 2;
	}

	if(ref == 0.0)
	{
		return 0;
	}

	// add ref to nds array
	self->ways_nds_array[self->ways_nds_idx] = ref;
	++self->ways_nds_idx;

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
osm_parser_insertRels(osm_parser_t* self,
                      int center, int polygon, int min_zoom)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_rid      = self->idx_insert_rels_rid;
	int idx_class    = self->idx_insert_rels_class;
	int idx_name     = self->idx_insert_rels_name;
	int idx_abrev    = self->idx_insert_rels_abrev;
	int idx_center   = self->idx_insert_rels_center;
	int idx_polygon  = self->idx_insert_rels_polygon;
	int idx_min_zoom = self->idx_insert_rels_min_zoom;
	int bytes_name   = strlen(self->tag_name)  + 1;
	int bytes_abrev  = strlen(self->tag_abrev) + 1;

	sqlite3_stmt* stmt = self->stmt_insert_rels;
	if((sqlite3_bind_double(stmt, idx_rid,
	                        self->attr_id) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_class,
	                     self->tag_class) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_name, self->tag_name,
	                      bytes_name, SQLITE_STATIC) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_abrev, self->tag_abrev,
	                      bytes_abrev, SQLITE_STATIC) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_center,
	                     center) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_polygon,
	                     polygon) != SQLITE_OK) ||
	   (sqlite3_bind_int(stmt, idx_min_zoom,
	                     min_zoom) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertRelsText(osm_parser_t* self)
{
	ASSERT(self);

	if(self->tag_text[0] == '\0')
	{
		return 1;
	}

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_rid    = self->idx_insert_rels_text_rid;
	int idx_txt   = self->idx_insert_rels_text_txt;
	int bytes_text = strlen(self->tag_text) + 1;

	sqlite3_stmt* stmt = self->stmt_insert_rels_text;
	if((sqlite3_bind_double(stmt, idx_rid,
	                        self->attr_id) != SQLITE_OK) ||
	   (sqlite3_bind_text(stmt, idx_txt, self->tag_text,
	                      bytes_text, SQLITE_STATIC) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertMembers(osm_parser_t* self,
                         osm_relationMember_t* m,
                         int idx)
{
	ASSERT(self);
	ASSERT(m);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_node_rid  = self->idx_insert_nodes_members_rid;
	int idx_node_nid  = self->idx_insert_nodes_members_nid;
	int idx_node_role = self->idx_insert_nodes_members_role;
	int idx_way_rid   = self->idx_insert_ways_members_rid;
	int idx_way_wid   = self->idx_insert_ways_members_wid;
	int idx_way_role  = self->idx_insert_ways_members_role;

	sqlite3_stmt* stmt = NULL;
	if(m->type && m->role && (m->ref != 0.0))
	{
		if(m->type == osmdb_relationMemberTypeToCode("node"))
		{
			stmt = self->stmt_insert_nodes_members;
			if((sqlite3_bind_double(stmt, idx_node_rid,
			                        self->attr_id) != SQLITE_OK) ||
			   (sqlite3_bind_double(stmt, idx_node_nid,
			                        m->ref) != SQLITE_OK) ||
			   (sqlite3_bind_int(stmt, idx_node_role,
			                     m->role) != SQLITE_OK))
			{
				LOGE("sqlite3_bind failed");
				return 0;
			}
		}
		else if(m->type == osmdb_relationMemberTypeToCode("way"))
		{
			stmt = self->stmt_insert_ways_members;
			if((sqlite3_bind_double(stmt, idx_way_rid,
			                        self->attr_id) != SQLITE_OK) ||
			   (sqlite3_bind_double(stmt, idx_way_wid,
			                        m->ref) != SQLITE_OK) ||
			   (sqlite3_bind_int(stmt, idx_way_role,
			                     m->role) != SQLITE_OK))
			{
				LOGE("sqlite3_bind failed");
				return 0;
			}
		}
		else
		{
			LOGW("invalid rid=%0.0lf, type=%i, role=%i, ref=%0.0lf",
			     self->attr_id, m->type, m->role, m->ref);
			return 1;
		}
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

static int
osm_parser_insertRelsRange(osm_parser_t* self,
                           double rid,
                           unsigned short l,
                           unsigned short r,
                           unsigned short b,
                           unsigned short t)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_rid = self->idx_insert_rels_range_rid;
	int idx_l   = self->idx_insert_rels_range_l;
	int idx_r   = self->idx_insert_rels_range_r;
	int idx_b   = self->idx_insert_rels_range_b;
	int idx_t   = self->idx_insert_rels_range_t;

	// adjust tile position to center since sqlite tweaks the
	// values outward slightly due to floating point precision
	double dl = ((double) l) + 0.5;
	double dr = ((double) r) + 0.5;
	double db = ((double) b) + 0.5;
	double dt = ((double) t) + 0.5;

	sqlite3_stmt* stmt = self->stmt_insert_rels_range;
	if((sqlite3_bind_double(stmt, idx_rid,
	                        rid) != SQLITE_OK)             ||
	   (sqlite3_bind_double(stmt, idx_l, dl) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_r, dr) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_b, db) != SQLITE_OK) ||
	   (sqlite3_bind_double(stmt, idx_t, dt) != SQLITE_OK))
	{
		LOGE("sqlite3_bind failed");
		return 0;
	}

	int ret = 1;
	if(sqlite3_step(stmt) != SQLITE_DONE)
	{
		LOGE("sqlite3_step failed");
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
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
	                       osmdb_classCodeToName(self->tag_class));
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
	if(selected == 0)
	{
		cc_listIter_t* iter = cc_list_head(self->rel_members);
		while(iter)
		{
			osm_relationMember_t* m;
			m = (osm_relationMember_t*)
			    cc_list_remove(self->rel_members, &iter);
			FREE(m);
		}

		return 1;
	}

	int min_zoom = sc ? osmdb_styleClass_minZoom(sc) : 999;
	if(osm_parser_insertRels(self, center,
	                         polygon, min_zoom) == 0)
	{
		return 0;
	}

	// add search text
	if(osm_parser_insertRelsText(self) == 0)
	{
		return 0;
	}

	// write rel members
	int idx = 0;
	cc_listIter_t* iter = cc_list_head(self->rel_members);
	while(iter)
	{
		osm_relationMember_t* m;
		m = (osm_relationMember_t*)
		    cc_list_remove(self->rel_members, &iter);
		if(m->type && m->role && (m->ref != 0.0))
		{
			if(osm_parser_insertMembers(self, m, idx) == 0)
			{
				return 0;
			}
			++idx;
		}
		FREE(m);
	}

	// update histogram
	++self->histogram[self->tag_class].rels;
	self->stats_relations += 1.0;
	if(osm_parser_logProgress(self))
	{
		double dt = self->t1 - self->t0;
		LOGI("dt=%0.0lf, line=%i, progress=%0.2f, relations=%0.0lf",
		     dt, line, 100.0f*progress,
		     self->stats_relations);
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
			char text[256];

			int class = osm_parser_findClass(self, atts[j], val);
			if(class)
			{
				// set or overwrite generic class
				if((self->tag_class == self->class_none)   ||
				   (self->tag_class == self->building_yes) ||
				   (self->tag_class == self->barrier_yes)  ||
				   (self->tag_class == self->office_yes)   ||
				   (self->tag_class == self->historic_yes) ||
				   (self->tag_class == self->man_made_yes) ||
				   (self->tag_class == self->tourism_yes))
				{
					self->tag_class = class;
				}
			}
			else if((strcmp(atts[j], "name") == 0) &&
			        (self->name_en == 0) &&
			        osm_parseName(line, val, name, abrev, text))
			{
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
				snprintf(self->tag_text,  256, "%s", text);
			}
			else if((strcmp(atts[j], "name:en") == 0) &&
			        osm_parseName(line, val, name, abrev, text))
			{
				self->name_en = 1;
				snprintf(self->tag_name,  256, "%s", name);
				snprintf(self->tag_abrev, 256, "%s", abrev);
				snprintf(self->tag_text,  256, "%s", text);
			}
			else if((strcmp(atts[j], "type") == 0))
			{
				self->rel_type = osmdb_relationTagTypeToCode(val);
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

	osm_relationMember_t* m;
	m = (osm_relationMember_t*)
	    CALLOC(1, sizeof(osm_relationMember_t));
	if(m == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	int i = 0;
	int j = 1;
	while(atts[i] && atts[j])
	{
		if(strcmp(atts[i], "type")  == 0)
		{
			m->type = osmdb_relationMemberTypeToCode(atts[j]);
		}
		else if(strcmp(atts[i], "ref")  == 0)
		{
			m->ref = strtod(atts[j], NULL);
		}
		else if(strcmp(atts[i], "role")  == 0)
		{
			m->role = osmdb_relationMemberRoleToCode(atts[j]);
		}

		i += 2;
		j += 2;
	}

	if(cc_list_append(self->rel_members, NULL,
	                  (const void*) m) == NULL)
	{
		FREE(m);
		return 0;
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
               const char* db_name,
               const char* tbl_name)
{
	ASSERT(style);
	ASSERT(db_name);
	ASSERT(tbl_name);

	osm_parser_t* self = (osm_parser_t*)
	                     CALLOC(1, sizeof(osm_parser_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->t0 = cc_timestamp();
	self->t1 = self->t0;

	if(sqlite3_initialize() != SQLITE_OK)
	{
		LOGE("sqlite3_initialize failed");
		goto fail_init;
	}

	if(sqlite3_open_v2(db_name, &self->db,
	                   SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE,
	                   NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_open_v2 %s failed", db_name);
		goto fail_open;
	}

	sqlite3_enable_load_extension(self->db, 1);
	if(sqlite3_load_extension(self->db, "./spellfix.so",
	                          NULL, NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_load_extension failed");
		goto fail_extension;
	}

	if(osm_parser_createTables(self) == 0)
	{
		goto fail_createTables;
	}

	const char* sql_begin = "BEGIN;";
	if(sqlite3_prepare_v2(self->db, sql_begin, -1,
	                      &self->stmt_begin,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_begin;
	}

	const char* sql_end = "END;";
	if(sqlite3_prepare_v2(self->db, sql_end, -1,
	                      &self->stmt_end,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_end;
	}

	const char* sql_rollback = "ROLLBACK;";
	if(sqlite3_prepare_v2(self->db, sql_rollback, -1,
	                      &self->stmt_rollback,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_rollback;
	}

	const char* sql_select_rels = "SELECT rid FROM tbl_rels;";
	if(sqlite3_prepare_v2(self->db, sql_select_rels, -1,
	                      &self->stmt_select_rels,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_rels;
	}

	const char* sql_select_rels_range =
		"SELECT min(l), max(r), min(b), max(t)"
		"	FROM tbl_ways_members"
		"	JOIN tbl_ways_range USING (wid)"
		"	WHERE rid=@arg_rid;";
	if(sqlite3_prepare_v2(self->db, sql_select_rels_range, -1,
	                      &self->stmt_select_rels_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_select_rels_range;
	}

	const char* sql_insert_class_rank =
		"INSERT INTO tbl_class_rank (class, rank)"
		"	VALUES (@arg_class, @arg_rank);";
	if(sqlite3_prepare_v2(self->db, sql_insert_class_rank, -1,
	                      &self->stmt_insert_class_rank,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_class_rank;
	}

	const char* sql_insert_nodes_coords =
		"INSERT INTO tbl_nodes_coords (nid, lat, lon)"
		"	VALUES (@arg_nid, @arg_lat, @arg_lon);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodes_coords, -1,
	                      &self->stmt_insert_nodes_coords,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodes_coords;
	}

	const char* sql_insert_nodes_info =
		"INSERT INTO tbl_nodes_info (nid, class, name, abrev, ele, st, min_zoom)"
		"	VALUES (@arg_nid, @arg_class, @arg_name, @arg_abrev, @arg_ele, @arg_st, @arg_min_zoom);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodes_info, -1,
	                      &self->stmt_insert_nodes_info,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodes_info;
	}

	const char* sql_insert_ways =
		"INSERT INTO tbl_ways (wid, class, layer, name, abrev, oneway, bridge, tunnel, cutting, center, polygon, selected, min_zoom, nds)"
		"	VALUES (@arg_wid, @arg_class, @arg_layer, @arg_name, @arg_abrev, @arg_oneway, @arg_bridge, @arg_tunnel, @arg_cutting, @arg_center, @arg_polygon, @arg_selected, @arg_min_zoom, @arg_nds);";
	if(sqlite3_prepare_v2(self->db, sql_insert_ways, -1,
	                      &self->stmt_insert_ways,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_ways;
	}

	const char* sql_insert_rels =
		"INSERT INTO tbl_rels (rid, class, name, abrev, center, polygon, min_zoom)"
		"	VALUES (@arg_rid, @arg_class, @arg_name, @arg_abrev, @arg_center, @arg_polygon, @arg_min_zoom);";
	if(sqlite3_prepare_v2(self->db, sql_insert_rels, -1,
	                      &self->stmt_insert_rels,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_rels;
	}

	const char* sql_insert_nodes_members =
		"INSERT INTO tbl_nodes_members (rid, nid, role)"
		"	VALUES (@arg_rid, @arg_nid, @arg_role);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodes_members, -1,
	                      &self->stmt_insert_nodes_members,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodes_members;
	}

	const char* sql_insert_ways_members =
		"INSERT INTO tbl_ways_members (idx, rid, wid, role)"
		"	VALUES (@arg_idx, @arg_rid, @arg_wid, @arg_role);";
	if(sqlite3_prepare_v2(self->db, sql_insert_ways_members, -1,
	                      &self->stmt_insert_ways_members,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_ways_members;
	}

	const char* sql_insert_nodes_range =
		"INSERT INTO tbl_nodes_range (nid, l, r, b, t)"
		"	VALUES (@arg_nid, @arg_l, @arg_r, @arg_b, @arg_t);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodes_range, -1,
	                      &self->stmt_insert_nodes_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodes_range;
	}

	const char* sql_insert_ways_range =
		"INSERT INTO tbl_ways_range (wid, l, r, b, t)"
		"	VALUES (@arg_wid, @arg_l, @arg_r, @arg_b, @arg_t);";
	if(sqlite3_prepare_v2(self->db, sql_insert_ways_range, -1,
	                      &self->stmt_insert_ways_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_ways_range;
	}

	const char* sql_insert_rels_range =
		"INSERT INTO tbl_rels_range (rid, l, r, b, t)"
		"	VALUES (@arg_rid, @arg_l, @arg_r, @arg_b, @arg_t);";
	if(sqlite3_prepare_v2(self->db, sql_insert_rels_range, -1,
	                      &self->stmt_insert_rels_range,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_rels_range;
	}

	const char* sql_insert_nodes_text =
		"INSERT INTO tbl_nodes_text (nid, txt)"
		"	VALUES (@arg_nid, @arg_txt);";
	if(sqlite3_prepare_v2(self->db, sql_insert_nodes_text, -1,
	                      &self->stmt_insert_nodes_text,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_nodes_txt;
	}

	const char* sql_insert_ways_text =
		"INSERT INTO tbl_ways_text (wid, txt)"
		"	VALUES (@arg_wid, @arg_txt);";
	if(sqlite3_prepare_v2(self->db, sql_insert_ways_text, -1,
	                      &self->stmt_insert_ways_text,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_ways_txt;
	}

	const char* sql_insert_rels_text =
		"INSERT INTO tbl_rels_text (rid, txt)"
		"	VALUES (@arg_rid, @arg_txt);";
	if(sqlite3_prepare_v2(self->db, sql_insert_rels_text, -1,
	                      &self->stmt_insert_rels_text,
	                      NULL) != SQLITE_OK)
	{
		LOGE("sqlite3_prepare_v2: %s", sqlite3_errmsg(self->db));
		goto fail_prepare_insert_rels_text;
	}

	self->idx_select_rels_range_rid      = sqlite3_bind_parameter_index(self->stmt_select_rels_range, "@arg_rid");
	self->idx_insert_class_rank_class    = sqlite3_bind_parameter_index(self->stmt_insert_class_rank, "@arg_class");
	self->idx_insert_class_rank_rank     = sqlite3_bind_parameter_index(self->stmt_insert_class_rank, "@arg_rank");
	self->idx_insert_nodes_coords_nid    = sqlite3_bind_parameter_index(self->stmt_insert_nodes_coords, "@arg_nid");
	self->idx_insert_nodes_coords_lat    = sqlite3_bind_parameter_index(self->stmt_insert_nodes_coords, "@arg_lat");
	self->idx_insert_nodes_coords_lon    = sqlite3_bind_parameter_index(self->stmt_insert_nodes_coords, "@arg_lon");
	self->idx_insert_nodes_info_nid      = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_nid");
	self->idx_insert_nodes_info_class    = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_class");
	self->idx_insert_nodes_info_name     = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_name");
	self->idx_insert_nodes_info_abrev    = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_abrev");
	self->idx_insert_nodes_info_ele      = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_ele");
	self->idx_insert_nodes_info_st       = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_st");
	self->idx_insert_nodes_info_min_zoom = sqlite3_bind_parameter_index(self->stmt_insert_nodes_info, "@arg_min_zoom");
	self->idx_insert_ways_wid            = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_wid");
	self->idx_insert_ways_class          = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_class");
	self->idx_insert_ways_layer          = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_layer");
	self->idx_insert_ways_name           = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_name");
	self->idx_insert_ways_abrev          = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_abrev");
	self->idx_insert_ways_oneway         = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_oneway");
	self->idx_insert_ways_bridge         = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_bridge");
	self->idx_insert_ways_tunnel         = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_tunnel");
	self->idx_insert_ways_cutting        = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_cutting");
	self->idx_insert_ways_center         = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_center");
	self->idx_insert_ways_polygon        = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_polygon");
	self->idx_insert_ways_selected       = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_selected");
	self->idx_insert_ways_min_zoom       = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_min_zoom");
	self->idx_insert_ways_nds            = sqlite3_bind_parameter_index(self->stmt_insert_ways, "@arg_nds");
	self->idx_insert_rels_rid            = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_rid");
	self->idx_insert_rels_class          = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_class");
	self->idx_insert_rels_name           = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_name");
	self->idx_insert_rels_abrev          = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_abrev");
	self->idx_insert_rels_center         = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_center");
	self->idx_insert_rels_polygon        = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_polygon");
	self->idx_insert_rels_min_zoom       = sqlite3_bind_parameter_index(self->stmt_insert_rels, "@arg_min_zoom");
	self->idx_insert_nodes_members_rid   = sqlite3_bind_parameter_index(self->stmt_insert_nodes_members, "@arg_rid");
	self->idx_insert_nodes_members_nid   = sqlite3_bind_parameter_index(self->stmt_insert_nodes_members, "@arg_nid");
	self->idx_insert_nodes_members_role  = sqlite3_bind_parameter_index(self->stmt_insert_nodes_members, "@arg_role");
	self->idx_insert_ways_members_idx    = sqlite3_bind_parameter_index(self->stmt_insert_ways_members, "@arg_idx");
	self->idx_insert_ways_members_rid    = sqlite3_bind_parameter_index(self->stmt_insert_ways_members, "@arg_rid");
	self->idx_insert_ways_members_wid    = sqlite3_bind_parameter_index(self->stmt_insert_ways_members, "@arg_wid");
	self->idx_insert_ways_members_role   = sqlite3_bind_parameter_index(self->stmt_insert_ways_members, "@arg_role");
	self->idx_insert_nodes_range_nid     = sqlite3_bind_parameter_index(self->stmt_insert_nodes_range, "@arg_nid");
	self->idx_insert_nodes_range_l       = sqlite3_bind_parameter_index(self->stmt_insert_nodes_range, "@arg_l");
	self->idx_insert_nodes_range_r       = sqlite3_bind_parameter_index(self->stmt_insert_nodes_range, "@arg_r");
	self->idx_insert_nodes_range_b       = sqlite3_bind_parameter_index(self->stmt_insert_nodes_range, "@arg_b");
	self->idx_insert_nodes_range_t       = sqlite3_bind_parameter_index(self->stmt_insert_nodes_range, "@arg_t");
	self->idx_insert_ways_range_wid      = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_wid");
	self->idx_insert_ways_range_l        = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_l");
	self->idx_insert_ways_range_r        = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_r");
	self->idx_insert_ways_range_b        = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_b");
	self->idx_insert_ways_range_t        = sqlite3_bind_parameter_index(self->stmt_insert_ways_range, "@arg_t");
	self->idx_insert_rels_range_rid      = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_rid");
	self->idx_insert_rels_range_l        = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_l");
	self->idx_insert_rels_range_r        = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_r");
	self->idx_insert_rels_range_b        = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_b");
	self->idx_insert_rels_range_t        = sqlite3_bind_parameter_index(self->stmt_insert_rels_range, "@arg_t");
	self->idx_insert_nodes_text_nid      = sqlite3_bind_parameter_index(self->stmt_insert_nodes_text, "@arg_nid");
	self->idx_insert_nodes_text_txt      = sqlite3_bind_parameter_index(self->stmt_insert_nodes_text, "@arg_txt");
	self->idx_insert_ways_text_wid       = sqlite3_bind_parameter_index(self->stmt_insert_ways_text, "@arg_wid");
	self->idx_insert_ways_text_txt       = sqlite3_bind_parameter_index(self->stmt_insert_ways_text, "@arg_txt");
	self->idx_insert_rels_text_rid       = sqlite3_bind_parameter_index(self->stmt_insert_rels_text, "@arg_rid");
	self->idx_insert_rels_text_txt       = sqlite3_bind_parameter_index(self->stmt_insert_rels_text, "@arg_txt");

	self->style = osmdb_style_newFile(style);
	if(self->style == NULL)
	{
		goto fail_style;
	}

	self->ways_nds_count = 16;
	self->ways_nds_array = (double*)
	                       CALLOC(self->ways_nds_count,
	                              sizeof(double));
	if(self->ways_nds_array == NULL)
	{
		goto fail_ways_nds_array;
	}

	self->rel_members = cc_list_new();
	if(self->rel_members == NULL)
	{
		goto fail_rel_members;
	}

	int cnt = osmdb_classCount();
	self->histogram = (osm_classHistogram_t*)
	                  CALLOC(cnt,
	                         sizeof(osm_classHistogram_t));
	if(self->histogram == NULL)
	{
		LOGE("CALLOC failed");
		goto fail_histogram;
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

	int    flags = O_RDWR | O_CREAT | O_EXCL;
	mode_t mode  = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	self->page_table = osmdb_table_open(tbl_name, flags, mode);
	if(self->page_table == NULL)
	{
		goto fail_page_table;
	}

	self->page_list = cc_list_new();
	if(self->page_list == NULL)
	{
		goto fail_page_list;
	}

	self->page_map = cc_map_new();
	if(self->page_map == NULL)
	{
		goto fail_page_map;
	}

	self->class_none   = osmdb_classKVToCode("class",    "none");
	self->building_yes = osmdb_classKVToCode("building", "yes");
	self->barrier_yes  = osmdb_classKVToCode("barrier",  "yes");
	self->office_yes   = osmdb_classKVToCode("office",   "yes");
	self->historic_yes = osmdb_classKVToCode("historic", "yes");
	self->man_made_yes = osmdb_classKVToCode("man_made", "yes");
	self->tourism_yes  = osmdb_classKVToCode("tourism",  "yes");

	// success
	return self;

	// failure
	fail_page_map:
		cc_list_delete(&self->page_list);
	fail_page_list:
		osmdb_table_close(&self->page_table);
	fail_page_table:
		iconv_close(self->cd);
	fail_iconv_open:
		osm_parser_discardClass(self);
	fail_fill_class:
		cc_map_delete(&self->class_map);
	fail_class_map:
		FREE(self->histogram);
	fail_histogram:
		cc_list_delete(&self->rel_members);
	fail_rel_members:
		FREE(self->ways_nds_array);
	fail_ways_nds_array:
		osmdb_style_delete(&self->style);
	fail_style:
		sqlite3_finalize(self->stmt_insert_rels_text);
	fail_prepare_insert_rels_text:
		sqlite3_finalize(self->stmt_insert_ways_text);
	fail_prepare_insert_ways_txt:
		sqlite3_finalize(self->stmt_insert_nodes_text);
	fail_prepare_insert_nodes_txt:
		sqlite3_finalize(self->stmt_insert_rels_range);
	fail_prepare_insert_rels_range:
		sqlite3_finalize(self->stmt_insert_ways_range);
	fail_prepare_insert_ways_range:
		sqlite3_finalize(self->stmt_insert_nodes_range);
	fail_prepare_insert_nodes_range:
		sqlite3_finalize(self->stmt_insert_ways_members);
	fail_prepare_insert_ways_members:
		sqlite3_finalize(self->stmt_insert_nodes_members);
	fail_prepare_insert_nodes_members:
		sqlite3_finalize(self->stmt_insert_rels);
	fail_prepare_insert_rels:
		sqlite3_finalize(self->stmt_insert_ways);
	fail_prepare_insert_ways:
		sqlite3_finalize(self->stmt_insert_nodes_info);
	fail_prepare_insert_nodes_info:
		sqlite3_finalize(self->stmt_insert_nodes_coords);
	fail_prepare_insert_nodes_coords:
		sqlite3_finalize(self->stmt_insert_class_rank);
	fail_prepare_insert_class_rank:
		sqlite3_finalize(self->stmt_select_rels_range);
	fail_prepare_select_rels_range:
		sqlite3_finalize(self->stmt_select_rels);
	fail_prepare_select_rels:
		sqlite3_finalize(self->stmt_rollback);
	fail_prepare_rollback:
		sqlite3_finalize(self->stmt_end);
	fail_prepare_end:
		sqlite3_finalize(self->stmt_begin);
	fail_prepare_begin:
	fail_createTables:
	fail_extension:
	fail_open:
	{
		// close db even when open fails
		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(sqlite3_shutdown() != SQLITE_OK)
		{
			LOGW("sqlite3_shutdown failed");
		}
	}
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
		osmdb_page_t*  page;
		cc_listIter_t* iter;
		cc_mapIter_t   miterator;
		cc_mapIter_t*  miter;
		miter = cc_map_head(self->page_map, &miterator);
		while(miter)
		{
			iter = (cc_listIter_t*)
			       cc_map_remove(self->page_map, &miter);
			page = (osmdb_page_t*)
			       cc_list_remove(self->page_list, &iter);
			osmdb_table_put(self->page_table, &page);

			if(osm_parser_logProgress(self))
			{
				double dt = self->t1 - self->t0;
				LOGI("dt=%0.0lf, pages=%i",
				     dt, cc_list_size(self->page_list));
			}
		}

		cc_map_delete(&self->page_map);
		cc_list_delete(&self->page_list);
		osmdb_table_close(&self->page_table);
		iconv_close(self->cd);

		osm_parser_discardClass(self);
		cc_map_delete(&self->class_map);

		// print histogram
		double dt = cc_timestamp() - self->t0;
		LOGI("dt=%0.0lf, nodes=%0.0lf, ways=%0.0lf, relations=%0.0lf",
		     dt, self->stats_nodes, self->stats_ways,
		     self->stats_relations);
		int idx;
		int cnt = osmdb_classCount();
		for(idx = 0; idx < cnt; ++idx)
		{
			if(self->histogram[idx].nodes ||
			   self->histogram[idx].ways  ||
			   self->histogram[idx].rels)
			{
				LOGI("class=%s, nodes=%i, ways=%i, rels=%i",
				     osmdb_classCodeToName(idx),
				     self->histogram[idx].nodes,
				     self->histogram[idx].ways,
				     self->histogram[idx].rels);
			}
		}
		FREE(self->histogram);

		iter = cc_list_head(self->rel_members);
		while(iter)
		{
			osm_relationMember_t* m;
			m = (osm_relationMember_t*)
			    cc_list_remove(self->rel_members, &iter);
			FREE(m);
		}

		cc_list_delete(&self->rel_members);
		FREE(self->ways_nds_array);

		osmdb_style_delete(&self->style);

		sqlite3_finalize(self->stmt_insert_rels_text);
		sqlite3_finalize(self->stmt_insert_ways_text);
		sqlite3_finalize(self->stmt_insert_nodes_text);
		sqlite3_finalize(self->stmt_insert_rels_range);
		sqlite3_finalize(self->stmt_insert_ways_range);
		sqlite3_finalize(self->stmt_insert_nodes_range);
		sqlite3_finalize(self->stmt_insert_ways_members);
		sqlite3_finalize(self->stmt_insert_nodes_members);
		sqlite3_finalize(self->stmt_insert_rels);
		sqlite3_finalize(self->stmt_insert_ways);
		sqlite3_finalize(self->stmt_insert_nodes_info);
		sqlite3_finalize(self->stmt_insert_nodes_coords);
		sqlite3_finalize(self->stmt_insert_class_rank);
		sqlite3_finalize(self->stmt_select_rels_range);
		sqlite3_finalize(self->stmt_select_rels);
		sqlite3_finalize(self->stmt_rollback);
		sqlite3_finalize(self->stmt_end);
		sqlite3_finalize(self->stmt_begin);

		if(sqlite3_close_v2(self->db) != SQLITE_OK)
		{
			LOGW("sqlite3_close_v2 failed");
		}

		if(sqlite3_shutdown() != SQLITE_OK)
		{
			LOGW("sqlite3_shutdown failed");
		}

		FREE(self);
		*_self = NULL;
	}
}

int osm_parser_beginTransaction(osm_parser_t* self)
{
	ASSERT(self);

	int ret = 1;

	if(self->batch_size >= OSM_BATCH_SIZE_MAX)
	{
		if(osm_parser_endTransaction(self) == 0)
		{
			return 0;
		}
	}
	else if(self->batch_size > 0)
	{
		++self->batch_size;
		return 1;
	}

	sqlite3_stmt* stmt = self->stmt_begin;
	if(sqlite3_step(stmt) == SQLITE_DONE)
	{
		++self->batch_size;
	}
	else
	{
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

int osm_parser_endTransaction(osm_parser_t* self)
{
	ASSERT(self);

	if(self->batch_size == 0)
	{
		return 1;
	}

	int ret = 1;

	sqlite3_stmt* stmt = self->stmt_end;
	if(sqlite3_step(stmt) == SQLITE_DONE)
	{
		self->batch_size = 0;
	}
	else
	{
		ret = 0;
	}

	if(sqlite3_reset(stmt) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	return ret;
}

void osm_parser_rollbackTransaction(osm_parser_t* self)
{
	ASSERT(self);

	sqlite3_step(self->stmt_rollback);
	if(sqlite3_reset(self->stmt_rollback) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}
}

int osm_parser_createTables(osm_parser_t* self)
{
	ASSERT(self);

	const char* sql[] =
	{
		"PRAGMA temp_store_directory = '.';",
		"PRAGMA cache_size = 100000;",
		"CREATE TABLE tbl_class_rank"
		"("
		"	class INTEGER PRIMARY KEY NOT NULL,"
		"	rank  INTEGER"
		");",
		"CREATE TABLE tbl_nodes_coords"
		"("
		"	nid      INTEGER PRIMARY KEY NOT NULL,"
		"	lat      FLOAT,"
		"	lon      FLOAT"
		");",
		"CREATE TABLE tbl_nodes_info"
		"("
		"	nid      INTEGER PRIMARY KEY NOT NULL REFERENCES tbl_nodes_coords,"
		"	class    INTEGER REFERENCES tbl_class_rank,"
		"	name     TEXT,"
		"	abrev    TEXT,"
		"	ele      INTEGER,"
		"	st       INTEGER,"
		"	min_zoom INTEGER"
		");",
		"CREATE TABLE tbl_ways"
		"("
		"	wid      INTEGER PRIMARY KEY NOT NULL,"
		"	class    INTEGER REFERENCES tbl_class_rank,"
		"	layer    INTEGER,"
		"	name     TEXT,"
		"	abrev    TEXT,"
		"	oneway   INTEGER,"
		"	bridge   INTEGER,"
		"	tunnel   INTEGER,"
		"	cutting  INTEGER,"
		"	center   INTEGER,"
		"	polygon  INTEGER,"
		"	selected INTEGER,"
		"	min_zoom INTEGER,"
		"	nds      BLOB"
		");",
		"CREATE TABLE tbl_rels"
		"("
		"	rid      INTEGER PRIMARY KEY NOT NULL,"
		"	class    INTEGER REFERENCES tbl_class_rank,"
		"	name     TEXT,"
		"	abrev    TEXT,"
		"	center   INTEGER,"
		"	polygon  INTEGER,"
		"	min_zoom INTEGER"
		");",
		"CREATE TABLE tbl_nodes_members"
		"("
		"	rid  INTEGER REFERENCES tbl_rels,"
		"	nid  INTEGER REFERENCES tbl_nodes_coords,"
		"	role INTEGER"
		");",
		"CREATE TABLE tbl_ways_members"
		"("
		"	idx  INTEGER,"
		"	rid  INTEGER REFERENCES tbl_rels,"
		"	wid  INTEGER REFERENCES tbl_ways,"
		"	role INTEGER"
		");",
		"CREATE VIRTUAL TABLE tbl_nodes_range USING rtree"
		"("
		"	nid,"
		"	l,"
		"	r,"
		"	b,"
		"	t"
		");",
		"CREATE VIRTUAL TABLE tbl_ways_range USING rtree"
		"("
		"	wid,"
		"	l,"
		"	r,"
		"	b,"
		"	t"
		");",
		"CREATE VIRTUAL TABLE tbl_rels_range USING rtree"
		"("
		"	rid,"
		"	l,"
		"	r,"
		"	b,"
		"	t"
		");",
		"CREATE VIRTUAL TABLE tbl_nodes_text USING fts4(nid, txt);",
		"CREATE VIRTUAL TABLE tbl_nodes_aux  USING fts4aux(tbl_nodes_text);",
		"CREATE VIRTUAL TABLE tbl_ways_text  USING fts4(wid, txt);",
		"CREATE VIRTUAL TABLE tbl_ways_aux   USING fts4aux(tbl_ways_text);",
		"CREATE VIRTUAL TABLE tbl_rels_text  USING fts4(rid, txt);",
		"CREATE VIRTUAL TABLE tbl_rels_aux   USING fts4aux(tbl_rels_text);",
		"CREATE VIRTUAL TABLE tbl_spellfix   USING spellfix1;",
		NULL
	};

	int idx = 0;
	while(sql[idx])
	{
		if(sqlite3_exec(self->db, sql[idx], NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec(%i): %s",
			     idx, sqlite3_errmsg(self->db));
			return 0;
		}
		++idx;
	}

	return 1;
}

int osm_parser_createIndices(osm_parser_t* self)
{
	ASSERT(self);

	const char* sql[] =
	{
		"CREATE INDEX idx_ways_members ON tbl_ways_members (rid);",
		NULL
	};

	int idx = 0;
	while(sql[idx])
	{
		if(sqlite3_exec(self->db, sql[idx], NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec idx=%i failed", idx);
			return 0;
		}
		++idx;
	}

	return 1;
}

int osm_parser_initRangeRels(osm_parser_t* self)
{
	ASSERT(self);

	double s = 0.0;
	double n = self->stats_relations;

	int idx_rid = self->idx_select_rels_range_rid;

	sqlite3_stmt* stmt1 = self->stmt_select_rels;
	sqlite3_stmt* stmt2 = self->stmt_select_rels_range;
	while(sqlite3_step(stmt1) == SQLITE_ROW)
	{
		double rid = sqlite3_column_double(stmt1, 0);

		if(osm_parser_beginTransaction(self) == 0)
		{
			goto fail_transaction;
		}

		if(sqlite3_bind_double(stmt2, idx_rid, rid) != SQLITE_OK)
		{
			goto fail_bind;
		}

		unsigned short l;
		unsigned short r;
		unsigned short b;
		unsigned short t;
		while(sqlite3_step(stmt2) == SQLITE_ROW)
		{
			l = (unsigned short) sqlite3_column_int(stmt2, 0);
			r = (unsigned short) sqlite3_column_int(stmt2, 1);
			b = (unsigned short) sqlite3_column_int(stmt2, 2);
			t = (unsigned short) sqlite3_column_int(stmt2, 3);
			if(osm_parser_insertRelsRange(self, rid,
			                              l, r, b, t) == 0)
			{
				goto fail_insert;
			}
		}

		if(sqlite3_reset(stmt2) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}

		s = s + 1.0;

		if(osm_parser_logProgress(self))
		{
			double dt = self->t1 - self->t0;
			LOGI("dt=%0.0lf, progress=%0.2lf", dt, 100.0*s/n);
		}
	}

	if(sqlite3_reset(stmt1) != SQLITE_OK)
	{
		LOGW("sqlite3_reset failed");
	}

	// success
	return 1;

	// failure
	fail_insert:
	{
		if(sqlite3_reset(stmt2) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	fail_bind:
	fail_transaction:
	{
		osm_parser_endTransaction(self);
		if(sqlite3_reset(stmt1) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}
	return 0;
}

int osm_parser_initClassRank(osm_parser_t* self)
{
	ASSERT(self);

	if(osm_parser_beginTransaction(self) == 0)
	{
		return 0;
	}

	int idx_class = self->idx_insert_class_rank_class;
	int idx_rank  = self->idx_insert_class_rank_rank;

	sqlite3_stmt* stmt = self->stmt_insert_class_rank;

	int code;
	int rank;
	int count = osmdb_classCount();
	for(code = 0; code < count; ++code)
	{
		rank = osmdb_classCodeToRank(code);
		if((sqlite3_bind_int(stmt, idx_class, code) != SQLITE_OK) ||
		   (sqlite3_bind_int(stmt, idx_rank, rank) != SQLITE_OK))
		{
			LOGE("sqlite3_bind failed");
			goto fail_bind;
		}

		if(sqlite3_step(stmt) != SQLITE_DONE)
		{
			LOGE("sqlite3_step failed");
			goto fail_step;
		}

		if(sqlite3_reset(stmt) != SQLITE_OK)
		{
			LOGW("sqlite3_reset failed");
		}
	}

	// success
	return osm_parser_endTransaction(self);

	// failure
	fail_step:
	fail_bind:
		osm_parser_rollbackTransaction(self);
	return 0;
}

int osm_parser_initRange(osm_parser_t* self)
{
	ASSERT(self);

	if(osm_parser_initRangeRels(self) == 0)
	{
		osm_parser_rollbackTransaction(self);
		return 0;
	}

	return osm_parser_endTransaction(self);
}

int osm_parser_initSearch(osm_parser_t* self)
{
	ASSERT(self);

	const char* sql[] =
	{
		"INSERT INTO tbl_spellfix(word)"
		"	SELECT term FROM tbl_nodes_aux WHERE col='*';",
		"INSERT INTO tbl_spellfix(word)"
		"	SELECT term FROM tbl_ways_aux WHERE col='*';",
		"INSERT INTO tbl_spellfix(word)"
		"	SELECT term FROM tbl_rels_aux WHERE col='*';",
		"DROP TABLE tbl_rels_aux;",
		"DROP TABLE tbl_ways_aux;",
		"DROP TABLE tbl_nodes_aux;",
		NULL
	};

	int idx = 0;
	while(sql[idx])
	{
		if(sqlite3_exec(self->db, sql[idx], NULL, NULL,
		                NULL) != SQLITE_OK)
		{
			LOGE("sqlite3_exec idx=%i failed", idx);
			return 0;
		}

		double dt = cc_timestamp() - self->t0;
		LOGI("dt=%0.0lf, idx=%i", dt, idx);
		++idx;
	}

	return 1;
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
		osm_parser_rollbackTransaction(self);
		return 0;
	}

	return osm_parser_endTransaction(self);
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

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
#include <string.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "../libpak/pak_file.h"
#include "../libxmlstream/xml_istream.h"
#include "osmdb_style.h"

/***********************************************************
* private                                                  *
***********************************************************/

static void osmdb_style_finish(osmdb_style_t* self)
{
	ASSERT(self);

	cc_mapIter_t  iterator;
	cc_mapIter_t* iter;
	iter = cc_map_head(self->classes, &iterator);
	while(iter)
	{
		osmdb_styleClass_t* class;
		class = (osmdb_styleClass_t*)
		        cc_map_remove(self->classes, &iter);
		FREE(class);
	}

	iter = cc_map_head(self->polys, &iterator);
	while(iter)
	{
		osmdb_stylePolygon_t* poly;
		poly = (osmdb_stylePolygon_t*)
		       cc_map_remove(self->polys, &iter);
		FREE(poly);
	}

	iter = cc_map_head(self->lines, &iterator);
	while(iter)
	{
		osmdb_styleLine_t* line;
		line = (osmdb_styleLine_t*)
		       cc_map_remove(self->lines, &iter);
		FREE(line);
	}

	iter = cc_map_head(self->points, &iterator);
	while(iter)
	{
		osmdb_stylePoint_t* point;
		point = (osmdb_stylePoint_t*)
		        cc_map_remove(self->points, &iter);
		FREE(point);
	}

	iter = cc_map_head(self->colors, &iterator);
	while(iter)
	{
		cc_vec4f_t* color;
		color = (cc_vec4f_t*)
		        cc_map_remove(self->colors, &iter);
		cc_vec4f_delete(&color);
	}

	iter = cc_map_head(self->layers, &iterator);
	while(iter)
	{
		int* layerp = (int*)
		              cc_map_remove(self->layers, &iter);
		FREE(layerp);
	}
}

/***********************************************************
* private - parser                                         *
***********************************************************/

static int osmdb_style_parseLineMode(const char* mode)
{
	ASSERT(mode);

	char str[256];
	int  m   = 0;
	int  src = 0;
	int  dst = 0;
	while(1)
	{
		str[dst] = mode[src];
		if((str[dst] == ',') || (str[dst] == '\0'))
		{
			// parse the mode
			str[dst] = '\0';

			if(strcmp(str, "dashed") == 0)
			{
				m |= OSMDB_STYLE_MODE_DASHED;
			}
			else if(strcmp(str, "striped") == 0)
			{
				m |= OSMDB_STYLE_MODE_STRIPED;
			}

			// end of string
			if(mode[src] == '\0')
			{
				break;
			}

			// next character
			dst = 0;
			++src;
			continue;
		}

		// next character
		++dst;
		++src;
	}

	return m;
}

static int
osmdb_style_beginOsm(osmdb_style_t* self,
                     int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_OSM;

	return 1;
}

static int
osmdb_style_beginOsmLayer(osmdb_style_t* self,
                          int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_LAYER;

	const char* name = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(name == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	int* layerp = (int*) CALLOC(1, sizeof(int));
	if(layerp == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}
	*layerp = cc_map_size(self->layers);

	if(cc_map_add(self->layers, (const void*) layerp,
	              name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		FREE(layerp);
	return 0;
}

static int
osmdb_style_beginOsmColor(osmdb_style_t* self,
                          int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_COLOR;

	const char* name = NULL;
	const char* val  = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "val") == 0)
		{
			val = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((name == NULL) || (val == NULL))
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	unsigned int v = (unsigned int) strtoll(val, NULL, 16);
	float r = ((float) ((v >> 24) & 0xFF))/255.0f;
	float g = ((float) ((v >> 16) & 0xFF))/255.0f;
	float b = ((float) ((v >> 8) & 0xFF))/255.0f;
	float a = ((float) (v & 0xFF))/255.0f;

	cc_vec4f_t* c = cc_vec4f_new(r, g, b, a);
	if(c == NULL)
	{
		return 0;
	}

	if(cc_map_add(self->colors, (const void*) c,
	              name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		cc_vec4f_delete(&c);
	return 0;
}

static int
osmdb_style_beginOsmPoint(osmdb_style_t* self,
                          int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_POINT;

	const char* name          = NULL;
	const char* min_zoom      = NULL;
	const char* text_color1   = NULL;
	const char* text_color2   = NULL;
	const char* marker_color1 = NULL;
	const char* marker_color2 = NULL;
	const char* flags         = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "min_zoom") == 0)
		{
			min_zoom = atts[idx1];
		}
		else if(strcmp(atts[idx0], "text_color1") == 0)
		{
			text_color1 = atts[idx1];
		}
		else if(strcmp(atts[idx0], "text_color2") == 0)
		{
			text_color2 = atts[idx1];
		}
		else if(strcmp(atts[idx0], "marker_color1") == 0)
		{
			marker_color1 = atts[idx1];
		}
		else if(strcmp(atts[idx0], "marker_color2") == 0)
		{
			marker_color2 = atts[idx1];
		}
		else if(strcmp(atts[idx0], "flags") == 0)
		{
			flags = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(name == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	cc_mapIter_t iter;
	cc_vec4f_t* tc1 = NULL;
	if(text_color1)
	{
		tc1 = (cc_vec4f_t*)
		      cc_map_find(self->colors, &iter, text_color1);
		if(tc1 == NULL)
		{
			LOGE("invalid line=%i text_color1=%s",
			     line, text_color1);
			return 0;
		}
	}

	cc_vec4f_t* tc2 = NULL;
	if(text_color2)
	{
		tc2 = (cc_vec4f_t*)
		      cc_map_find(self->colors, &iter, text_color2);
		if(tc2 == NULL)
		{
			LOGE("invalid line=%i text_color2=%s",
			     line, text_color2);
			return 0;
		}
	}

	cc_vec4f_t* mc1 = NULL;
	if(marker_color1)
	{
		mc1 = (cc_vec4f_t*)
		      cc_map_find(self->colors, &iter, marker_color1);
		if(mc1 == NULL)
		{
			LOGE("invalid line=%i marker_color1=%s",
			     line, marker_color1);
			return 0;
		}
	}

	cc_vec4f_t* mc2 = NULL;
	if(marker_color2)
	{
		mc2 = (cc_vec4f_t*)
		      cc_map_find(self->colors, &iter, marker_color2);
		if(mc2 == NULL)
		{
			LOGE("invalid line=%i marker_color2=%s",
			     line, marker_color2);
			return 0;
		}
	}

	int mz = 11;
	if(min_zoom)
	{
		mz = (int) strtol(min_zoom, NULL, 0);
	}

	// parse flags
	int show_ele    = 0;
	int show_marker = 0;
	if(flags)
	{
		char str[256];
		int  src = 0;
		int  dst = 0;
		while(dst < 256)
		{
			str[dst] = flags[src];
			if((str[dst] == ' ') || (str[dst] == '\t'))
			{
				// discard whitespace
				++src;
				continue;
			}
			else if((str[dst] == ',') || (str[dst] == '\0'))
			{
				// parse the flag
				str[dst] = '\0';
				if(strcmp(str, "ele:show") == 0)
				{
					show_ele = 1;
				}
				else if(strcmp(str, "marker:show") == 0)
				{
					show_marker = 1;
				}
				else
				{
					LOGW("unknown flag=%s", str);
				}

				// end of string
				if(flags[src] == '\0')
				{
					break;
				}

				// next character
				dst = 0;
				++src;
				continue;
			}

			// next character
			++dst;
			++src;
		}
	}

	osmdb_stylePoint_t* point;
	point = (osmdb_stylePoint_t*)
	        CALLOC(1, sizeof(osmdb_stylePoint_t));
	if(point == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	point->min_zoom      = mz;
	point->show_ele      = show_ele;
	point->show_marker   = show_marker;
	point->text_color1   = tc1;
	point->text_color2   = tc2;
	point->marker_color1 = mc1;
	point->marker_color2 = mc2;

	if(cc_map_add(self->points, (const void*) point,
	              name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		FREE(point);
	return 0;
}

static int
osmdb_style_beginOsmLine(osmdb_style_t* self,
                         int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_LINE;

	const char* name   = NULL;
	const char* mode   = NULL;
	const char* color1 = NULL;
	const char* color2 = NULL;
	const char* width  = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "mode") == 0)
		{
			mode = atts[idx1];
		}
		else if(strcmp(atts[idx0], "color1") == 0)
		{
			color1 = atts[idx1];
		}
		else if(strcmp(atts[idx0], "color2") == 0)
		{
			color2 = atts[idx1];
		}
		else if(strcmp(atts[idx0], "width") == 0)
		{
			width = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(name == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	int modei = OSMDB_STYLE_MODE_SOLID;
	if(mode)
	{
		modei = osmdb_style_parseLineMode(mode);
	}

	cc_vec4f_t* c1 = NULL;
	cc_mapIter_t iter;
	if(color1)
	{
		c1 = (cc_vec4f_t*)
		     cc_map_find(self->colors, &iter, color1);
		if(c1 == NULL)
		{
			LOGE("invalid line=%i", line);
			return 0;
		}
	}

	cc_vec4f_t* c2 = NULL;
	if(color2)
	{
		c2 = (cc_vec4f_t*)
		     cc_map_find(self->colors, &iter, color2);
		if(c2 == NULL)
		{
			LOGE("invalid line=%i", line);
			return 0;
		}
	}

	float w = 1.0f;
	if(width)
	{
		w = strtod(width, NULL);
	}

	osmdb_styleLine_t* linep;
	linep = (osmdb_styleLine_t*)
	        CALLOC(1, sizeof(osmdb_styleLine_t));
	if(linep == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}
	linep->width  = w;
	linep->mode   = modei;
	linep->color1 = c1;
	linep->color2 = c2;

	if(cc_map_add(self->lines, (const void*) linep,
	              name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		FREE(linep);
	return 0;
}

static int
osmdb_style_beginOsmPoly(osmdb_style_t* self,
                         int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_POLY;

	const char* name  = NULL;
	const char* color = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "color") == 0)
		{
			color = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(name == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	cc_vec4f_t* c = NULL;
	cc_mapIter_t iter;
	if(color)
	{
		c = (cc_vec4f_t*)
		    cc_map_find(self->colors, &iter, color);
		if(c == NULL)
		{
			LOGE("invalid line=%i color=%s", line, color);
			return 0;
		}
	}

	osmdb_stylePolygon_t* poly;
	poly = (osmdb_stylePolygon_t*)
	       CALLOC(1, sizeof(osmdb_stylePolygon_t));
	if(poly == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}
	poly->color = c;

	if(cc_map_add(self->polys, (const void*) poly,
	              name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		FREE(poly);
	return 0;
}

static int
osmdb_style_beginOsmClass(osmdb_style_t* self,
                          int line, const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STYLE_STATE_CLASS;

	const char* name  = NULL;
	const char* layer = NULL;
	const char* ln    = NULL;
	const char* poly  = NULL;
	const char* point = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "layer") == 0)
		{
			layer = atts[idx1];
		}
		else if(strcmp(atts[idx0], "line") == 0)
		{
			ln = atts[idx1];
		}
		else if(strcmp(atts[idx0], "poly") == 0)
		{
			poly = atts[idx1];
		}
		else if(strcmp(atts[idx0], "point") == 0)
		{
			point = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(name == NULL)
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	int layeri = 0;
	cc_mapIter_t iter;
	if(layer)
	{
		int* layerp = (int*)
		              cc_map_find(self->layers, &iter, layer);
		if(layerp == NULL)
		{
			LOGE("invalid line=%i", line);
			return 0;
		}
		layeri = *layerp;
	}

	osmdb_styleLine_t* linep = NULL;
	if(ln)
	{
		linep = (osmdb_styleLine_t*)
		        cc_map_find(self->lines, &iter, ln);
		if(linep == NULL)
		{
			LOGE("invalid line=%i", line);
			return 0;
		}
	}

	osmdb_stylePolygon_t* polyp = NULL;
	if(poly)
	{
		polyp = (osmdb_stylePolygon_t*)
		        cc_map_find(self->polys, &iter, poly);
		if(polyp == NULL)
		{
			LOGE("invalid line=%i, poly=%s", line, poly);
			return 0;
		}
	}

	osmdb_stylePoint_t* pointp = NULL;
	if(point)
	{
		pointp = (osmdb_stylePoint_t*)
		         cc_map_find(self->points, &iter, point);
		if(pointp == NULL)
		{
			LOGE("invalid line=%i, point=%s", line, point);
			return 0;
		}
	}

	osmdb_styleClass_t* class;
	class = (osmdb_styleClass_t*)
	        CALLOC(1, sizeof(osmdb_styleClass_t));
	if(class == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}
	class->layer = layeri;
	class->line  = linep;
	class->poly  = polyp;
	class->point = pointp;

	if(cc_map_add(self->classes, (const void*) class,
	              name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		FREE(class);
	return 0;
}

static int
osmdb_style_start(void* priv, int line,
                  const char* name, const char** atts)
{
	ASSERT(priv);
	ASSERT(name);
	ASSERT(atts);

	osmdb_style_t* self = (osmdb_style_t*) priv;

	int state = self->state;
	if(state == OSMDB_STYLE_STATE_INIT)
	{
		if(strcmp(name, "osmdb") == 0)
		{
			return osmdb_style_beginOsm(self, line, atts);
		}
	}
	else if(state == OSMDB_STYLE_STATE_OSM)
	{
		if(strcmp(name, "layer") == 0)
		{
			return osmdb_style_beginOsmLayer(self, line, atts);
		}
		else if(strcmp(name, "color") == 0)
		{
			return osmdb_style_beginOsmColor(self, line, atts);
		}
		else if(strcmp(name, "line") == 0)
		{
			return osmdb_style_beginOsmLine(self, line, atts);
		}
		else if(strcmp(name, "poly") == 0)
		{
			return osmdb_style_beginOsmPoly(self, line, atts);
		}
		else if(strcmp(name, "point") == 0)
		{
			return osmdb_style_beginOsmPoint(self, line, atts);
		}
		else if(strcmp(name, "class") == 0)
		{
			return osmdb_style_beginOsmClass(self, line, atts);
		}
	}

	LOGE("state=%i, name=%s, line=%i", state, name, line);
	return 0;
}

static int
osmdb_style_end(void* priv, int line,
                const char* name, const char* content)
{
	// content may be NULL
	ASSERT(priv);
	ASSERT(name);

	osmdb_style_t* self = (osmdb_style_t*) priv;

	if(self->state == OSMDB_STYLE_STATE_DONE)
	{
		return 0;
	}
	else if(self->state == OSMDB_STYLE_STATE_OSM)
	{
		self->state = OSMDB_STYLE_STATE_DONE;
	}
	else
	{
		self->state = OSMDB_STYLE_STATE_OSM;
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_style_t*
osmdb_style_new(const char* resource, const char* fname)
{
	ASSERT(resource);
	ASSERT(fname);

	osmdb_style_t* self;
	self = (osmdb_style_t*)
	       CALLOC(1, sizeof(osmdb_style_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}
	self->state = OSMDB_STYLE_STATE_INIT;

	self->layers = cc_map_new();
	if(self->layers == NULL)
	{
		goto fail_layers;
	}

	self->colors = cc_map_new();
	if(self->colors == NULL)
	{
		goto fail_colors;
	}

	self->points = cc_map_new();
	if(self->points == NULL)
	{
		goto fail_points;
	}

	self->lines = cc_map_new();
	if(self->lines == NULL)
	{
		goto fail_lines;
	}

	self->polys = cc_map_new();
	if(self->polys == NULL)
	{
		goto fail_polys;
	}

	self->classes = cc_map_new();
	if(self->classes == NULL)
	{
		goto fail_classes;
	}

	pak_file_t* pak = pak_file_open(resource, PAK_FLAG_READ);
	if(pak == NULL)
	{
		LOGE("invalid %s", resource);
		goto fail_pak;
	}

	int len = pak_file_seek(pak, fname);
	if(len == 0)
	{
		LOGE("invalid %s", fname);
		goto fail_seek;
	}

	if(xml_istream_parseFile((void*) self,
	                         osmdb_style_start,
	                         osmdb_style_end,
	                         pak->f, len) == 0)
	{
		goto fail_parse;
	}

	pak_file_close(&pak);

	// success
	return self;

	// failure
	fail_parse:
		osmdb_style_finish(self);
	fail_seek:
		pak_file_close(&pak);
	fail_pak:
		cc_map_delete(&self->classes);
	fail_classes:
		cc_map_delete(&self->polys);
	fail_polys:
		cc_map_delete(&self->lines);
	fail_lines:
		cc_map_delete(&self->points);
	fail_points:
		cc_map_delete(&self->colors);
	fail_colors:
		cc_map_delete(&self->layers);
	fail_layers:
		FREE(self);
	return NULL;
}

osmdb_style_t* osmdb_style_newFile(const char* fname)
{
	ASSERT(fname);

	osmdb_style_t* self;
	self = (osmdb_style_t*) CALLOC(1, sizeof(osmdb_style_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}
	self->state = OSMDB_STYLE_STATE_INIT;

	self->layers = cc_map_new();
	if(self->layers == NULL)
	{
		goto fail_layers;
	}

	self->colors = cc_map_new();
	if(self->colors == NULL)
	{
		goto fail_colors;
	}

	self->points = cc_map_new();
	if(self->points == NULL)
	{
		goto fail_points;
	}

	self->lines = cc_map_new();
	if(self->lines == NULL)
	{
		goto fail_lines;
	}

	self->polys = cc_map_new();
	if(self->polys == NULL)
	{
		goto fail_polys;
	}

	self->classes = cc_map_new();
	if(self->classes == NULL)
	{
		goto fail_classes;
	}

	if(xml_istream_parse((void*) self,
	                     osmdb_style_start,
	                     osmdb_style_end,
	                     fname) == 0)
	{
		goto fail_parse;
	}

	// success
	return self;

	// failure
	fail_parse:
		osmdb_style_finish(self);
		cc_map_delete(&self->classes);
	fail_classes:
		cc_map_delete(&self->polys);
	fail_polys:
		cc_map_delete(&self->lines);
	fail_lines:
		cc_map_delete(&self->points);
	fail_points:
		cc_map_delete(&self->colors);
	fail_colors:
		cc_map_delete(&self->layers);
	fail_layers:
		FREE(self);
	return NULL;
}

void osmdb_style_delete(osmdb_style_t** _self)
{
	ASSERT(_self);

	osmdb_style_t* self = *_self;
	if(self)
	{
		osmdb_style_finish(self);
		cc_map_delete(&self->classes);
		cc_map_delete(&self->polys);
		cc_map_delete(&self->lines);
		cc_map_delete(&self->points);
		cc_map_delete(&self->colors);
		cc_map_delete(&self->layers);
		FREE(self);
		*_self = NULL;
	}
}

osmdb_styleClass_t* osmdb_style_class(osmdb_style_t* self,
                                      const char* name)
{
	ASSERT(self);
	ASSERT(name);

	cc_mapIter_t iter;
	return (osmdb_styleClass_t*)
	       cc_map_find(self->classes, &iter, name);
}

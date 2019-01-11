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
#include "../libxmlstream/xml_istream.h"
#include "../libpak/pak_file.h"
#include "osmdb_style.h"

#define LOG_TAG "osmdb"
#include "../libxmlstream/xml_log.h"

/***********************************************************
* private                                                  *
***********************************************************/

static void osmdb_style_finish(osmdb_style_t* self)
{
	assert(self);

	a3d_hashmapIter_t  iterator;
	a3d_hashmapIter_t* iter;
	iter = a3d_hashmap_head(self->classes, &iterator);
	while(iter)
	{
		osmdb_styleClass_t* class;
		class = (osmdb_styleClass_t*)
		        a3d_hashmap_remove(self->classes, &iter);
		free(class);
	}

	iter = a3d_hashmap_head(self->polys, &iterator);
	while(iter)
	{
		osmdb_stylePolygon_t* poly;
		poly = (osmdb_stylePolygon_t*)
		       a3d_hashmap_remove(self->polys, &iter);
		free(poly);
	}

	iter = a3d_hashmap_head(self->lines, &iterator);
	while(iter)
	{
		osmdb_styleLine_t* line;
		line = (osmdb_styleLine_t*)
		       a3d_hashmap_remove(self->lines, &iter);
		free(line);
	}

	iter = a3d_hashmap_head(self->colors, &iterator);
	while(iter)
	{
		a3d_vec4f_t* color;
		color = (a3d_vec4f_t*)
		        a3d_hashmap_remove(self->colors, &iter);
		a3d_vec4f_delete(&color);
	}

	iter = a3d_hashmap_head(self->layers, &iterator);
	while(iter)
	{
		int* layerp = (int*)
		              a3d_hashmap_remove(self->layers, &iter);
		free(layerp);
	}
}

/***********************************************************
* private - parser                                         *
***********************************************************/

static int osmdb_style_parseLineMode(const char* mode)
{
	assert(mode);

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
	assert(self);
	assert(atts);

	self->state = OSMDB_STYLE_STATE_OSM;

	return 1;
}

static int
osmdb_style_beginOsmLayer(osmdb_style_t* self,
                          int line, const char** atts)
{
	assert(self);
	assert(atts);

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

	int* layerp = (int*) malloc(sizeof(int));
	if(layerp == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	*layerp = a3d_hashmap_size(self->layers);

	if(a3d_hashmap_add(self->layers,
	                   (const void*) layerp, name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		free(layerp);
	return 0;
}

static int
osmdb_style_beginOsmColor(osmdb_style_t* self,
                          int line, const char** atts)
{
	assert(self);
	assert(atts);

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

	a3d_vec4f_t* c = a3d_vec4f_new(r, g, b, a);
	if(c == NULL)
	{
		return 0;
	}

	if(a3d_hashmap_add(self->colors,
	                   (const void*) c, name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		a3d_vec4f_delete(&c);
	return 0;
}

static int
osmdb_style_beginOsmLine(osmdb_style_t* self,
                         int line, const char** atts)
{
	assert(self);
	assert(atts);

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

	a3d_vec4f_t* c1 = NULL;
	a3d_hashmapIter_t iter;
	if(color1)
	{
		c1 = (a3d_vec4f_t*)
		     a3d_hashmap_find(self->colors, &iter, color1);
		if(c1 == NULL)
		{
			LOGE("invalid line=%i", line);
			return 0;
		}
	}

	a3d_vec4f_t* c2 = NULL;
	if(color2)
	{
		c2 = (a3d_vec4f_t*)
		     a3d_hashmap_find(self->colors, &iter, color2);
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

	osmdb_styleLine_t* linep = (osmdb_styleLine_t*)
	                           malloc(sizeof(osmdb_styleLine_t));
	if(linep == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	linep->width  = w;
	linep->mode   = modei;
	linep->color1 = c1;
	linep->color2 = c2;

	if(a3d_hashmap_add(self->lines,
	                   (const void*) linep, name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		free(linep);
	return 0;
}

static int
osmdb_style_beginOsmPoly(osmdb_style_t* self,
                         int line, const char** atts)
{
	assert(self);
	assert(atts);

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

	a3d_vec4f_t* c = NULL;
	a3d_hashmapIter_t iter;
	if(color)
	{
		c = (a3d_vec4f_t*)
		    a3d_hashmap_find(self->colors, &iter, color);
		if(c == NULL)
		{
			LOGE("invalid line=%i color=%s", line, color);
			return 0;
		}
	}

	osmdb_stylePolygon_t* poly = (osmdb_stylePolygon_t*)
	                             malloc(sizeof(osmdb_stylePolygon_t));
	if(poly == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	poly->color = c;

	if(a3d_hashmap_add(self->polys,
	                   (const void*) poly, name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		free(poly);
	return 0;
}

static int
osmdb_style_beginOsmClass(osmdb_style_t* self,
                          int line, const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STYLE_STATE_CLASS;

	const char* name  = NULL;
	const char* layer = NULL;
	const char* ln    = NULL;
	const char* poly  = NULL;

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
	a3d_hashmapIter_t iter;
	if(layer)
	{
		int* layerp = (int*)
		              a3d_hashmap_find(self->layers, &iter, layer);
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
		        a3d_hashmap_find(self->lines, &iter, ln);
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
		        a3d_hashmap_find(self->polys, &iter, poly);
		if(polyp == NULL)
		{
			LOGE("invalid line=%i, poly=%s", line, poly);
			return 0;
		}
	}

	osmdb_styleClass_t* class = (osmdb_styleClass_t*)
	                            malloc(sizeof(osmdb_styleClass_t));
	if(class == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}
	class->layer = layeri;
	class->line  = linep;
	class->poly  = polyp;

	if(a3d_hashmap_add(self->classes,
	                   (const void*) class, name) == 0)
	{
		goto fail_add;
	}

	// success
	return 1;

	// failure
	fail_add:
		free(class);
	return 0;
}

static int osmdb_style_start(void* priv,
                             int line,
                             const char* name,
                             const char** atts)
{
	assert(priv);
	assert(name);
	assert(atts);

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
		else if(strcmp(name, "class") == 0)
		{
			return osmdb_style_beginOsmClass(self, line, atts);
		}
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

static int osmdb_style_end(void* priv,
                           int line,
                           const char* name,
                           const char* content)
{
	// content may be NULL
	assert(priv);
	assert(name);

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

osmdb_style_t* osmdb_style_new(const char* fname)
{
	assert(fname);

	osmdb_style_t* self = (osmdb_style_t*)
	                      malloc(sizeof(osmdb_style_t));
	if(self == NULL)
	{
		LOGE("malloc failed");
		return NULL;
	}
	self->state = OSMDB_STYLE_STATE_INIT;

	self->layers = a3d_hashmap_new();
	if(self->layers == NULL)
	{
		goto fail_layers;
	}

	self->colors = a3d_hashmap_new();
	if(self->colors == NULL)
	{
		goto fail_colors;
	}

	self->lines = a3d_hashmap_new();
	if(self->lines == NULL)
	{
		goto fail_lines;
	}

	self->polys = a3d_hashmap_new();
	if(self->polys == NULL)
	{
		goto fail_polys;
	}

	self->classes = a3d_hashmap_new();
	if(self->classes == NULL)
	{
		goto fail_classes;
	}

	pak_file_t* pak = pak_file_open(fname, PAK_FLAG_READ);
	if(pak == NULL)
	{
		goto fail_pak;
	}

	int len = pak_file_seek(pak, "style.xml");
	if(len == 0)
	{
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
		a3d_hashmap_delete(&self->classes);
	fail_classes:
		a3d_hashmap_delete(&self->polys);
	fail_polys:
		a3d_hashmap_delete(&self->lines);
	fail_lines:
		a3d_hashmap_delete(&self->colors);
	fail_colors:
		a3d_hashmap_delete(&self->layers);
	fail_layers:
		free(self);
	return NULL;
}

void osmdb_style_delete(osmdb_style_t** _self)
{
	assert(_self);

	osmdb_style_t* self = *_self;
	if(self)
	{
		osmdb_style_finish(self);
		a3d_hashmap_delete(&self->classes);
		a3d_hashmap_delete(&self->polys);
		a3d_hashmap_delete(&self->lines);
		a3d_hashmap_delete(&self->colors);
		a3d_hashmap_delete(&self->layers);
		free(self);
		*_self = NULL;
	}
}

osmdb_styleClass_t* osmdb_style_class(osmdb_style_t* self,
                                      const char* name)
{
	assert(self);
	assert(name);

	a3d_hashmapIter_t iter;
	return (osmdb_styleClass_t*)
	       a3d_hashmap_find(self->classes, &iter, name);
}

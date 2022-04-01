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

#ifndef osmdb_style_H
#define osmdb_style_H

#include "../libcc/cc_map.h"
#include "../libcc/math/cc_vec4f.h"

#define OSMDB_STYLE_STATE_INIT  0
#define OSMDB_STYLE_STATE_OSM   1
#define OSMDB_STYLE_STATE_LAYER 2
#define OSMDB_STYLE_STATE_COLOR 3
#define OSMDB_STYLE_STATE_POINT 4
#define OSMDB_STYLE_STATE_LINE  5
#define OSMDB_STYLE_STATE_POLY  6
#define OSMDB_STYLE_STATE_CLASS 7
#define OSMDB_STYLE_STATE_DONE  8

#define OSMDB_STYLE_MODE_SOLID   0
#define OSMDB_STYLE_MODE_DASHED  1
#define OSMDB_STYLE_MODE_STRIPED 2
#define OSMDB_STYLE_MODE_NAMED   4

typedef struct
{
	int min_zoom;
	int show_ele;
	int show_marker;

	float       text_scale;
	cc_vec4f_t* text_color1;
	cc_vec4f_t* text_color2;
	cc_vec4f_t* marker_color1;
	cc_vec4f_t* marker_color2;
} osmdb_stylePoint_t;

typedef struct
{
	// mode: solid|dashed|striped|dashed,striped
	// mode may also include the named flag which instructs
	// the importer to discard unnamed ways of this class
	int   min_zoom;
	float width;
	int   mode;
	cc_vec4f_t* color1;
	cc_vec4f_t* color2;
} osmdb_styleLine_t;

typedef struct
{
	int         min_zoom;
	cc_vec4f_t* color;
} osmdb_stylePolygon_t;

// See wiki for expected feature types when developing style sheet
// https://wiki.openstreetmap.org/wiki/Map_Features
typedef struct
{
	int abrev;
	int layer;

	osmdb_styleLine_t*    line;
	osmdb_stylePolygon_t* poly;
	osmdb_stylePoint_t*   point;
} osmdb_styleClass_t;

int osmdb_styleClass_minZoom(osmdb_styleClass_t* self);

typedef struct
{
	int state;

	// map from name to layer number
	cc_map_t* layers;

	// map from name to object
	cc_map_t* colors;
	cc_map_t* points;
	cc_map_t* lines;
	cc_map_t* polys;
	cc_map_t* classes;
} osmdb_style_t;

osmdb_style_t*      osmdb_style_new(const char* resource,
                                    const char* fname);
osmdb_style_t*      osmdb_style_newFile(const char* fname);
void                osmdb_style_delete(osmdb_style_t** _self);
osmdb_styleClass_t* osmdb_style_class(osmdb_style_t* self,
                                      const char* name);

#endif

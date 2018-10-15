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

#include "../a3d/a3d_hashmap.h"
#include "../a3d/math/a3d_vec4f.h"

#define OSMDB_STYLE_STATE_INIT  0
#define OSMDB_STYLE_STATE_OSM   1
#define OSMDB_STYLE_STATE_LAYER 2
#define OSMDB_STYLE_STATE_COLOR 3
#define OSMDB_STYLE_STATE_LINE  4
#define OSMDB_STYLE_STATE_POLY  5
#define OSMDB_STYLE_STATE_CLASS 6
#define OSMDB_STYLE_STATE_DONE  7

#define OSMDB_STYLE_MODE_SOLID   0
#define OSMDB_STYLE_MODE_DASHED  1
#define OSMDB_STYLE_MODE_STRIPED 2

typedef struct
{
	// mode: solid|dashed|striped|dashed,striped
	float width;
	int   mode;
	a3d_vec4f_t* color1;
	a3d_vec4f_t* color2;
} osmdb_styleLine_t;

typedef struct
{
	a3d_vec4f_t* color;
} osmdb_stylePolygon_t;

// See wiki for expected feature types when developing style sheet
// https://wiki.openstreetmap.org/wiki/Map_Features
typedef struct
{
	int layer;
	osmdb_styleLine_t*    line;
	osmdb_stylePolygon_t* poly;
} osmdb_styleClass_t;

typedef struct
{
	int state;

	// map from name to layer number
	a3d_hashmap_t* layers;

	// map from name to object
	a3d_hashmap_t* colors;
	a3d_hashmap_t* lines;
	a3d_hashmap_t* polys;
	a3d_hashmap_t* classes;
} osmdb_style_t;

osmdb_style_t*      osmdb_style_new(const char* fname);
void                osmdb_style_delete(osmdb_style_t** _self);
osmdb_styleClass_t* osmdb_style_class(osmdb_style_t* self,
                                      const char* name);

#endif

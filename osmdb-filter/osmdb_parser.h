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

#ifndef osmdb_parser_H
#define osmdb_parser_H

#include "a3d/a3d_hashmap.h"
#include "libxmlstream/xml_ostream.h"

// WRITE and NODE are the same mode
#define OSMDB_MODE_WRITE 0
#define OSMDB_MODE_NODE  0
#define OSMDB_MODE_WAY   1
#define OSMDB_MODE_REL   2

typedef struct
{
	int state;
	int mode;
	int selected;

	// key=class
	// val=class
	a3d_hashmap_t* classes;

	// key=id
	// val=node/way/rel name
	a3d_hashmap_t* nodes;
	a3d_hashmap_t* ways;
	a3d_hashmap_t* rels;

	xml_ostream_t* os;
} osmdb_parser_t;

osmdb_parser_t* osmdb_parser_new(a3d_hashmap_t* classes,
                                 const char* gzname);
void            osmdb_parser_delete(osmdb_parser_t** _self);
void            osmdb_parser_mode(osmdb_parser_t* self,
                                  int mode);
int             osmdb_parser_start(void* priv, int line,
                                   const char* name,
                                   const char** atts);
int             osmdb_parser_end(void* priv,
                                 int line,
                                 const char* name,
                                 const char* content);

#endif

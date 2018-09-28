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

#ifndef osmdb_node_H
#define osmdb_node_H

#include "../libxmlstream/xml_ostream.h"

typedef struct
{
	double id;
	double lat;
	double lon;
	char*  name;
	char*  abrev;
	int    ele;
	int    st;
	int    class;
} osmdb_node_t;

osmdb_node_t* osmdb_node_new(const char** atts, int line);
osmdb_node_t* osmdb_node_copy(osmdb_node_t* self);
void          osmdb_node_delete(osmdb_node_t** _self);
int           osmdb_node_export(osmdb_node_t* self,
                                xml_ostream_t* os);
int           osmdb_node_size(osmdb_node_t* self);

#endif

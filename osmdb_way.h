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

#ifndef osmdb_way_H
#define osmdb_way_H

#include "../a3d/a3d_list.h"
#include "../libxmlstream/xml_ostream.h"
#include "osmdb_range.h"

typedef struct
{
	int    refcount;
	double id;
	char*  name;
	char*  abrev;
	int    class;
	int    layer;
	int    oneway;
	int    bridge;
	int    tunnel;
	int    cutting;

	double latT;
	double lonL;
	double latB;
	double lonR;

	a3d_list_t* nds;
} osmdb_way_t;

osmdb_way_t* osmdb_way_new(const char** atts, int line);
osmdb_way_t* osmdb_way_copy(osmdb_way_t* self);
osmdb_way_t* osmdb_way_copyEmpty(osmdb_way_t* self);
void         osmdb_way_delete(osmdb_way_t** _self);
void         osmdb_way_incref(osmdb_way_t* self);
int          osmdb_way_decref(osmdb_way_t* self);
int          osmdb_way_export(osmdb_way_t* self,
                              xml_ostream_t* os);
int          osmdb_way_size(osmdb_way_t* self);
int          osmdb_way_nd(osmdb_way_t* self,
                          const char** atts, int line);
void         osmdb_way_updateRange(osmdb_way_t* self,
                                   osmdb_range_t* range);
int          osmdb_way_ref(osmdb_way_t* self,
                           double ref);
int          osmdb_way_join(osmdb_way_t* a, osmdb_way_t* b,
                            double ref1, double* ref2);
void         osmdb_way_discardNds(osmdb_way_t* self);

#endif

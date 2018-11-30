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

#ifndef osmdb_relation_H
#define osmdb_relation_H

#include "../a3d/a3d_list.h"
#include "../libxmlstream/xml_ostream.h"

typedef struct
{
	int    type;
	double ref;
	int    role;
} osmdb_member_t;

typedef struct
{
	int    refcount;
	double id;
	double lat;
	double lon;
	char*  name;
	char*  abrev;
	int    class;

	a3d_list_t* members;
} osmdb_relation_t;

osmdb_relation_t* osmdb_relation_new(const char** atts, int line);
osmdb_relation_t* osmdb_relation_copyCenter(osmdb_relation_t* self,
                                            double lat, double lon);
void              osmdb_relation_delete(osmdb_relation_t** _self);
void              osmdb_relation_incref(osmdb_relation_t* self);
int               osmdb_relation_decref(osmdb_relation_t* self);
int               osmdb_relation_export(osmdb_relation_t* self,
                                        xml_ostream_t* os);
int               osmdb_relation_size(osmdb_relation_t* self);
int               osmdb_relation_member(osmdb_relation_t* self,
                                        const char** atts,
                                        int line);

#endif

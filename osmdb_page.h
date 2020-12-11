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

#ifndef osmdb_page_H
#define osmdb_page_H

#include <sys/types.h>
#include <unistd.h>

#define OSMDB_PAGE_SIZE 4096

typedef struct
{
	int    dirty;
	off_t  base;
	double coords[512];
} osmdb_page_t;

// table functions
osmdb_page_t* osmdb_page_new(off_t base);
void          osmdb_page_delete(osmdb_page_t** _self);

// user functions
void osmdb_page_get(osmdb_page_t* self,
                    double id,
                    double* coord);
void osmdb_page_set(osmdb_page_t* self,
                    double id,
                    double* coord);

#endif

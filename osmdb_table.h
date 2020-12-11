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

#ifndef osmdb_table_H
#define osmdb_table_H

#include <sys/types.h>
#include <unistd.h>

#include "osmdb_page.h"

typedef struct
{
	int   fd;
	off_t size;
} osmdb_table_t;

osmdb_table_t* osmdb_table_open(const char* fname,
                                int flags, mode_t mode);
void           osmdb_table_close(osmdb_table_t** _self);
osmdb_page_t*  osmdb_table_get(osmdb_table_t* self,
                               off_t base);
int            osmdb_table_put(osmdb_table_t* self,
                               osmdb_page_t** _page);

#endif

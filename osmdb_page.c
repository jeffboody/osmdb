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

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "osmdb_page.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_page_t* osmdb_page_new(off_t base)
{
	ASSERT(base % OSMDB_PAGE_SIZE == 0);
	ASSERT(sizeof(double) == 8);

	osmdb_page_t* self;
	self = (osmdb_page_t*)
	       CALLOC(1, sizeof(osmdb_page_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->base = base;

	return self;
}

void osmdb_page_delete(osmdb_page_t** _self)
{
	ASSERT(_self);

	osmdb_page_t* self = *_self;
	if(self)
	{
		FREE(self);
		*_self = NULL;
	}
}

void osmdb_page_get(osmdb_page_t* self, double id,
                    double* coord)
{
	ASSERT(self);
	ASSERT(coord);

	// 16 bytes per coord
	off_t offset = 16*((off_t) id);
	int   idx0   = (int) (offset - self->base)/8;
	int   idx1   = idx0 + 1;

	coord[0] = self->coords[idx0];
	coord[1] = self->coords[idx1];
}

void osmdb_page_set(osmdb_page_t* self, double id,
                    double* coord)
{
	ASSERT(self);
	ASSERT(coord);

	// 16 bytes per coord
	off_t offset = 16*((off_t) id);
	int   idx0   = (int) (offset - self->base)/8;
	int   idx1   = idx0 + 1;

	self->dirty        = 1;
	self->coords[idx0] = coord[0];
	self->coords[idx1] = coord[1];
}

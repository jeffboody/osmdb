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

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "osmdb_index.h"
#include "osmdb_range.h"

/***********************************************************
* public                                                   *
***********************************************************/

void osmdb_range_init(osmdb_range_t* self)
{
	ASSERT(self);

	self->pts  = 0;
	self->latT = 0.0;
	self->lonL = 0.0;
	self->latB = 0.0;
	self->lonR = 0.0;
}

void osmdb_range_addPt(osmdb_range_t* self,
                       double lat, double lon)
{
	ASSERT(self);

	if(self->pts)
	{
		if(lat > self->latT)
		{
			self->latT = lat;
		}
		else if(lat < self->latB)
		{
			self->latB = lat;
		}

		if(lon < self->lonL)
		{
			self->lonL = lon;
		}
		else if(lon > self->lonR)
		{
			self->lonR = lon;
		}
	}
	else
	{
		self->latT = lat;
		self->lonL = lon;
		self->latB = lat;
		self->lonR = lon;
	}
	++self->pts;
}

int osmdb_range_clip(osmdb_range_t* self,
                     double latT, double lonL,
                     double latB, double lonR)
{
	ASSERT(self);

	if(self->pts == 0)
	{
		return 1;
	}

	if((self->latT < latB) ||
	   (self->latB > latT) ||
	   (self->lonL > lonR) ||
	   (self->lonR < lonL))
	{
		return 1;
	}

	return 0;
}

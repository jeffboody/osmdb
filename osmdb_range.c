/*
 * Copyright (c) 2022 Jeff Boody
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

#define LOG_TAG "osmdb"
#include "../libcc/math/cc_pow2n.h"
#include "../libcc/cc_log.h"
#include "osmdb_range.h"

/***********************************************************
* public                                                   *
***********************************************************/

void osmdb_range_init(osmdb_range_t* self,
                      int zoom, int x, int y)
{
	ASSERT(self);

	// tl: (0.0, 0.0) => (16383, -16384)
	// br: (1.0, 1.0) => (-16384, 16383)
	// short: -32768 => 32767

	// determine tile zoom
	int zoom0 = zoom;
	if(zoom >= 15)
	{
		zoom0 = 15;
	}
	else if((zoom > 3) && (zoom%2 == 0))
	{
		zoom0 = zoom - 1;
	}

	// compute the subtile index
	int n  = cc_pow2n(zoom - zoom0);
	int x0 = n*(x/n);
	int y0 = n*(y/n);
	int i  = y - y0;
	int j  = x - x0;
	int s  = 2*16384/n;

	// compute the subtile range
	self->t = 16383  - i*s;
	self->l = -16384 + j*s;
	self->b = -16384 + (n - 1 - i)*s;
	self->r = 16383  - (n - 1 - j)*s;
}

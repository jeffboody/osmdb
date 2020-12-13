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

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "osmdb_table.h"

/***********************************************************
* private                                                  *
***********************************************************/

#define OFF_T_ERR ((off_t) -1)

const unsigned short OSMDB_BLANK_COORDS[2048] = { 0 };

static int
osmdb_table_write(osmdb_table_t* self, osmdb_page_t* page)
{
	ASSERT(self);
	ASSERT(page);

	size_t bytes;
	size_t left   = OSMDB_PAGE_SIZE;
	size_t offset = 0;
	char*  buf    = NULL;

	// seek to the desired page or fill empty pages
	if(self->size >= page->base)
	{
		if(lseek(self->fd, page->base, SEEK_SET) == OFF_T_ERR)
		{
			LOGE("lseek: %s", strerror(errno));
			return 0;
		}
	}
	else
	{
		if(lseek(self->fd, (off_t) 0, SEEK_END) == OFF_T_ERR)
		{
			LOGE("lseek: %s", strerror(errno));
			return 0;
		}

		// write blank pages
		while(self->size < page->base)
		{
			left   = OSMDB_PAGE_SIZE;
			offset = 0;
			buf    = (char*) OSMDB_BLANK_COORDS;
			while(left)
			{
				bytes = write(self->fd,
				              (const void*) &(buf[offset]), left);
				if(bytes == -1)
				{
					LOGE("write: %s", strerror(errno));
					return 0;
				}
				left   -= bytes;
				offset += bytes;
			}
			self->size += OSMDB_PAGE_SIZE;
		}
	}

	// write page
	left   = OSMDB_PAGE_SIZE;
	offset = 0;
	buf    = (char*) page->tiles;
	while(left)
	{
		bytes = write(self->fd,
		              (const void*) &(buf[offset]), left);
		if(bytes == -1)
		{
			LOGE("write: %s", strerror(errno));
			return 0;
		}
		left   -= bytes;
		offset += bytes;
	}

	off_t cur = lseek(self->fd, (off_t) 0, SEEK_CUR);
	if(cur == OFF_T_ERR)
	{
		LOGE("lseek: %s", strerror(errno));
		return 0;
	}
	else if(cur > self->size)
	{
		self->size = cur;
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_table_t*
osmdb_table_open(const char* fname, int flags, mode_t mode)
{
	ASSERT(fname);

	if(sizeof(off_t) != 8)
	{
		LOGE("sizeof(off_t)=%i", (int) sizeof(off_t));
		return NULL;
	}

	osmdb_table_t* self;
	self = (osmdb_table_t*)
	       CALLOC(1, sizeof(osmdb_table_t));
	if(self == NULL)
	{
		LOGE("CALOC failed");
		return NULL;
	}

	self->fd = open(fname, flags, mode);
	if(self->fd == -1)
	{
		LOGE("open: %s", strerror(errno));
		goto fail_open;
	}

	self->size = lseek(self->fd, (off_t) 0, SEEK_END);
	if(self->size == OFF_T_ERR)
	{
		LOGE("lseek: %s", strerror(errno));
		goto fail_size;
	}

	if(lseek(self->fd, (off_t) 0, SEEK_SET) == OFF_T_ERR)
	{
		LOGE("lseek: %s", strerror(errno));
		goto fail_size;
	}

	// success
	return self;

	// failure
	fail_size:
		if(close(self->fd) != 0)
		{
			LOGE("close: %s", strerror(errno));
		}
	fail_open:
		FREE(self);
	return NULL;
}

void osmdb_table_close(osmdb_table_t** _self)
{
	ASSERT(_self);

	osmdb_table_t* self = *_self;
	if(self)
	{
		if(close(self->fd) != 0)
		{
			LOGE("close: %s", strerror(errno));
		}
		FREE(self);
		*_self = NULL;
	}
}

osmdb_page_t*
osmdb_table_get(osmdb_table_t* self, off_t base)
{
	ASSERT(self);

	osmdb_page_t* page;
	page = osmdb_page_new(base);
	if(page == NULL)
	{
		return NULL;
	}

	// return empty page if not in table yet
	if(self->size <= base)
	{
		return page;
	}

	if(lseek(self->fd, base, SEEK_SET) == OFF_T_ERR)
	{
		LOGE("lseek: %s", strerror(errno));
		goto fail_seek;
	}

	size_t bytes;
	size_t left   = OSMDB_PAGE_SIZE;
	size_t offset = 0;
	char*  buf    = (char*) page->tiles;
	while(left)
	{
		bytes = read(self->fd, (void*) &(buf[offset]), left);
		if(bytes == -1)
		{
			LOGE("read: %s", strerror(errno));
			goto fail_read;
		}
		left   -= bytes;
		offset += bytes;
	}

	// success
	return page;

	// failure
	fail_read:
	fail_seek:
		osmdb_page_delete(&page);
	return NULL;
}

int
osmdb_table_put(osmdb_table_t* self,
                osmdb_page_t** _page)
{
	ASSERT(self);
	ASSERT(_page);

	int ret = 1;

	osmdb_page_t* page = *_page;
	if(page)
	{
		if(page->dirty)
		{
			ret = osmdb_table_write(self, page);
		}

		osmdb_page_delete(_page);
	}

	return ret;
}

/*
 * Copyright (c) 2021 Jeff Boody
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "osmdb"
#include "libcc/cc_log.h"
#include "osmdb_blob.h"

/***********************************************************
* protected                                                *
***********************************************************/

void osmdb_blobNodeInfo_addName(osmdb_blobNodeInfo_t* self,
                                const char* name)
{
	ASSERT(self);
	ASSERT(name);

	// pad size_name to a multiple of 4 bytes
	int size_name = strlen(name) + 1;
	if(size_name == 1)
	{
		self->size_name = 0;
		return;
	}
	else if(size_name%4)
	{
		size_name = 4*(size_name/4 + 1);
	}
	self->size_name = size_name;

	char* s = osmdb_blobNodeInfo_name(self);
	snprintf(s, 256, "%s", name);
}

void osmdb_blobWayInfo_addName(osmdb_blobWayInfo_t* self,
                               const char* name)
{
	ASSERT(self);
	ASSERT(name);

	// pad size_name to a multiple of 4 bytes
	int size_name = strlen(name) + 1;
	if(size_name == 1)
	{
		self->size_name = 0;
		return;
	}
	else if(size_name%4)
	{
		size_name = 4*(size_name/4 + 1);
	}
	self->size_name = size_name;

	char* s = osmdb_blobWayInfo_name(self);
	snprintf(s, 256, "%s", name);
}

void osmdb_blobRelInfo_addName(osmdb_blobRelInfo_t* self,
                               const char* name)
{
	ASSERT(self);
	ASSERT(name);

	// pad size_name to a multiple of 4 bytes
	int size_name = strlen(name) + 1;
	if(size_name == 1)
	{
		self->size_name = 0;
		return;
	}
	else if(size_name%4)
	{
		size_name = 4*(size_name/4 + 1);
	}
	self->size_name = size_name;

	char* s = osmdb_blobRelInfo_name(self);
	snprintf(s, 256, "%s", name);
}

/***********************************************************
* public                                                   *
***********************************************************/

size_t
osmdb_blobNodeCoord_sizeof(osmdb_blobNodeCoord_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobNodeCoord_t);
}

char*
osmdb_blobNodeInfo_name(osmdb_blobNodeInfo_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_blobNodeInfo_t));
}

size_t
osmdb_blobNodeInfo_sizeof(osmdb_blobNodeInfo_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobNodeInfo_t) + self->size_name;
}

char*
osmdb_blobWayInfo_name(osmdb_blobWayInfo_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_blobWayInfo_t));
}

size_t
osmdb_blobWayInfo_sizeof(osmdb_blobWayInfo_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobWayInfo_t) + self->size_name;
}

size_t
osmdb_blobWayRange_sizeof(osmdb_blobWayRange_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobWayRange_t);
}

int64_t*
osmdb_blobWayNds_nds(osmdb_blobWayNds_t* self)
{
	ASSERT(self);

	return (int64_t*)
	       (((void*) self) + sizeof(osmdb_blobWayNds_t));
}

size_t
osmdb_blobWayNds_sizeof(osmdb_blobWayNds_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobWayNds_t) +
	       self->count*sizeof(int64_t);
}

char*
osmdb_blobRelInfo_name(osmdb_blobRelInfo_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_blobRelInfo_t));
}

size_t
osmdb_blobRelInfo_sizeof(osmdb_blobRelInfo_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobRelInfo_t) + self->size_name;
}

osmdb_blobRelData_t*
osmdb_blobRelMembers_data(osmdb_blobRelMembers_t* self)
{
	ASSERT(self);

	return (osmdb_blobRelData_t*)
	       (((void*) self) + sizeof(osmdb_blobRelMembers_t));
}

size_t
osmdb_blobRelMembers_sizeof(osmdb_blobRelMembers_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobRelMembers_t) +
	       self->count*sizeof(osmdb_blobRelData_t);
}

size_t
osmdb_blobRelRange_sizeof(osmdb_blobRelRange_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobRelRange_t);
}

int64_t* osmdb_blobTile_refs(osmdb_blobTile_t* self)
{
	ASSERT(self);

	return (int64_t*)
	       (((void*) self) + sizeof(osmdb_blobTile_t));
}

size_t osmdb_blobTile_sizeof(osmdb_blobTile_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_blobTile_t) +
	       self->count*sizeof(int64_t);
}

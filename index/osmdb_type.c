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
#include "osmdb_type.h"

/***********************************************************
* protected                                                *
***********************************************************/

void osmdb_nodeInfo_addName(osmdb_nodeInfo_t* self,
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

	char* s = osmdb_nodeInfo_name(self);

	// ensure the pad is cleared
	int i;
	for(i = (size_name - 4); i < size_name; ++i)
	{
		s[i] = '\0';
	}

	snprintf(s, size_name, "%s", name);
}

void osmdb_wayInfo_addName(osmdb_wayInfo_t* self,
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

	char* s = osmdb_wayInfo_name(self);

	// ensure the pad is cleared
	int i;
	for(i = (size_name - 4); i < size_name; ++i)
	{
		s[i] = '\0';
	}

	snprintf(s, size_name, "%s", name);
}

void osmdb_relInfo_addName(osmdb_relInfo_t* self,
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

	char* s = osmdb_relInfo_name(self);

	// ensure the pad is cleared
	int i;
	for(i = (size_name - 4); i < size_name; ++i)
	{
		s[i] = '\0';
	}

	snprintf(s, size_name, "%s", name);
}

/***********************************************************
* public                                                   *
***********************************************************/

size_t
osmdb_nodeCoord_sizeof(osmdb_nodeCoord_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_nodeCoord_t);
}

char*
osmdb_nodeInfo_name(osmdb_nodeInfo_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_nodeInfo_t));
}

size_t
osmdb_nodeInfo_sizeof(osmdb_nodeInfo_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_nodeInfo_t) + self->size_name;
}

char*
osmdb_wayInfo_name(osmdb_wayInfo_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_wayInfo_t));
}

size_t
osmdb_wayInfo_sizeof(osmdb_wayInfo_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_wayInfo_t) + self->size_name;
}

size_t
osmdb_wayRange_sizeof(osmdb_wayRange_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_wayRange_t);
}

int64_t*
osmdb_wayNds_nds(osmdb_wayNds_t* self)
{
	ASSERT(self);

	return (int64_t*)
	       (((void*) self) + sizeof(osmdb_wayNds_t));
}

size_t
osmdb_wayNds_sizeof(osmdb_wayNds_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_wayNds_t) +
	       self->count*sizeof(int64_t);
}

char*
osmdb_relInfo_name(osmdb_relInfo_t* self)
{
	ASSERT(self);

	if(self->size_name == 0)
	{
		return NULL;
	}

	return (char*)
	       (((void*) self) + sizeof(osmdb_relInfo_t));
}

size_t
osmdb_relInfo_sizeof(osmdb_relInfo_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_relInfo_t) + self->size_name;
}

osmdb_relData_t*
osmdb_relMembers_data(osmdb_relMembers_t* self)
{
	ASSERT(self);

	return (osmdb_relData_t*)
	       (((void*) self) + sizeof(osmdb_relMembers_t));
}

size_t
osmdb_relMembers_sizeof(osmdb_relMembers_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_relMembers_t) +
	       self->count*sizeof(osmdb_relData_t);
}

size_t
osmdb_relRange_sizeof(osmdb_relRange_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_relRange_t);
}

int64_t* osmdb_tileRefs_refs(osmdb_tileRefs_t* self)
{
	ASSERT(self);

	return (int64_t*)
	       (((void*) self) + sizeof(osmdb_tileRefs_t));
}

size_t osmdb_tileRefs_sizeof(osmdb_tileRefs_t* self)
{
	ASSERT(self);

	return sizeof(osmdb_tileRefs_t) +
	       self->count*sizeof(int64_t);
}

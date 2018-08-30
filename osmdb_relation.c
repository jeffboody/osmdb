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
#include <assert.h>
#include <string.h>
#include "osmdb_relation.h"
#include "osmdb_util.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_relation_t* osmdb_relation_new(const char** atts, int line)
{
	assert(atts);

	const char* id    = NULL;
	const char* name  = NULL;
	const char* abrev = NULL;
	const char* class = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "id") == 0)
		{
			id = atts[idx1];
		}
		else if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "abrev") == 0)
		{
			abrev = atts[idx1];
		}
		else if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(id == NULL)
	{
		LOGE("invalid line=%i", line);
		return NULL;
	}

	// create the relation
	osmdb_relation_t* self = (osmdb_relation_t*)
	                         calloc(1, sizeof(osmdb_relation_t));
	if(self == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	self->members = a3d_list_new();
	if(self->members == NULL)
	{
		goto fail_members;
	}

	self->id = strtod(id, NULL);

	if(name)
	{
		int len = strlen(name) + 1;
		self->name = (char*) malloc(len*sizeof(char));
		if(self->name == NULL)
		{
			goto fail_name;
		}
		snprintf(self->name, len, "%s", name);
	}

	if(abrev)
	{
		int len = strlen(abrev) + 1;
		self->abrev = (char*) malloc(len*sizeof(char));
		if(self->abrev == NULL)
		{
			goto fail_abrev;
		}
		snprintf(self->abrev, len, "%s", abrev);
	}

	if(class)
	{
		self->class = osmdb_classNameToCode(class);
	}

	// success
	return self;

	// failure
	fail_abrev:
		free(self->name);
	fail_name:
		a3d_list_delete(&self->members);
	fail_members:
		free(self);
	return NULL;
}

void osmdb_relation_delete(osmdb_relation_t** _self)
{
	assert(_self);

	osmdb_relation_t* self = *_self;
	if(self)
	{
		a3d_listitem_t* iter = a3d_list_head(self->members);
		while(iter)
		{
			osmdb_member_t* m = (osmdb_member_t*)
			                    a3d_list_remove(self->members, &iter);
			free(m);
		}

		a3d_list_delete(&self->members);
		free(self->name);
		free(self->abrev);
		free(self);
		*_self = NULL;
	}
}

int osmdb_relation_export(osmdb_relation_t* self,
                          xml_ostream_t* os)
{
	assert(self);
	assert(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "relation");
	ret &= xml_ostream_attrf(os, "id", "%0.0lf", self->id);
	if(self->name)
	{
		ret &= xml_ostream_attr(os, "name", self->name);
	}
	if(self->abrev)
	{
		ret &= xml_ostream_attr(os, "abrev", self->abrev);
	}
	if(self->class)
	{
		ret &= xml_ostream_attr(os, "class",
		                        osmdb_classCodeToName(self->class));
	}

	a3d_listitem_t* iter = a3d_list_head(self->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    a3d_list_peekitem(iter);
		ret &= xml_ostream_begin(os, "member");
		ret &= xml_ostream_attr(os, "type",
		                        osmdb_relationMemberCodeToType(m->type));
		ret &= xml_ostream_attrf(os, "ref", "%0.0lf", m->ref);
		if(m->role)
		{
			ret &= xml_ostream_attr(os, "role",
			                        osmdb_relationMemberCodeToType(m->role));
		}
		ret &= xml_ostream_end(os);
		iter = a3d_list_next(iter);
	}

	ret &= xml_ostream_end(os);

	return ret;
}

int osmdb_relation_size(osmdb_relation_t* self)
{
	assert(self);

	int size = sizeof(osmdb_relation_t);
	if(self->name)
	{
		size += strlen(self->name);
	}
	if(self->abrev)
	{
		size += strlen(self->abrev);
	}
	size += sizeof(osmdb_member_t)*a3d_list_size(self->members);

	return size;
}

int osmdb_relation_member(osmdb_relation_t* self,
                          const char** atts, int line)
{
	assert(self);
	assert(atts);

	const char* type = NULL;
	const char* ref  = NULL;
	const char* role = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "type") == 0)
		{
			type = atts[idx1];
		}
		else if(strcmp(atts[idx0], "ref") == 0)
		{
			ref = atts[idx1];
		}
		else if(strcmp(atts[idx0], "role") == 0)
		{
			role = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((type == NULL) || (ref == NULL))
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	// create the member
	osmdb_member_t* member = (osmdb_member_t*)
	                         calloc(1, sizeof(osmdb_member_t));
	if(member == NULL)
	{
		LOGE("calloc failed");
		return 0;
	}
	member->type = osmdb_relationMemberTypeToCode(type);
	member->ref  = strtod(ref, NULL);
	member->role = osmdb_relationMemberRoleToCode(role);

	// add member to list
	if(a3d_list_append(self->members, NULL,
	                   (const void*) member) == NULL)
	{
		goto fail_append;
	}

	// success
	return 1;

	// failure
	fail_append:
		free(member);
	return 0;
}

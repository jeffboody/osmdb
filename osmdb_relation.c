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
#include "../libxmlstream/xml_log.h"

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
	const char* latT  = NULL;
	const char* lonL  = NULL;
	const char* latB  = NULL;
	const char* lonR  = NULL;

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
		else if(strcmp(atts[idx0], "latT") == 0)
		{
			latT = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lonL") == 0)
		{
			lonL = atts[idx1];
		}
		else if(strcmp(atts[idx0], "latB") == 0)
		{
			latB = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lonR") == 0)
		{
			lonR = atts[idx1];
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

	self->members = cc_list_new();
	if(self->members == NULL)
	{
		goto fail_members;
	}

	self->refcount = 0;
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

	if(latT)
	{
		self->latT = strtod(latT, NULL);
	}

	if(lonL)
	{
		self->lonL = strtod(lonL, NULL);
	}

	if(latB)
	{
		self->latB = strtod(latB, NULL);
	}

	if(lonR)
	{
		self->lonR = strtod(lonR, NULL);
	}

	// success
	return self;

	// failure
	fail_abrev:
		free(self->name);
	fail_name:
		cc_list_delete(&self->members);
	fail_members:
		free(self);
	return NULL;
}

osmdb_relation_t*
osmdb_relation_copy(osmdb_relation_t* self)
{
	assert(self);

	osmdb_relation_t* copy;
	copy = osmdb_relation_copyEmpty(self);
	if(copy == NULL)
	{
		return NULL;
	}

	cc_listIter_t* iter = cc_list_head(self->members);
	while(iter)
	{
		osmdb_member_t* member;
		member = (osmdb_member_t*) cc_list_peekIter(iter);

		if(osmdb_relation_copyMember(copy, member) == 0)
		{
			goto fail_member;
		}

		iter = cc_list_next(iter);
	}

	// success
	return copy;

	// failure
	fail_member:
		osmdb_relation_delete(&copy);
	return NULL;
}

osmdb_relation_t*
osmdb_relation_copyEmpty(osmdb_relation_t* self)
{
	assert(self);

	osmdb_relation_t* copy;
	copy = (osmdb_relation_t*)
	       calloc(1, sizeof(osmdb_relation_t));
	if(copy == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	copy->members = cc_list_new();
	if(copy->members == NULL)
	{
		goto fail_members;
	}

	if(self->name)
	{
		int len = strlen(self->name) + 1;
		copy->name = (char*) malloc(len*sizeof(char));
		if(copy->name == NULL)
		{
			LOGE("malloc failed");
			goto fail_name;
		}
		snprintf(copy->name, len, "%s", self->name);
	}

	if(self->abrev)
	{
		int len = strlen(self->abrev) + 1;
		copy->abrev = (char*) malloc(len*sizeof(char));
		if(copy->abrev == NULL)
		{
			LOGE("malloc failed");
			goto fail_abrev;
		}
		snprintf(copy->abrev, len, "%s", self->abrev);
	}

	copy->id    = self->id;
	copy->class = self->class;
	copy->latT  = self->latT;
	copy->lonL  = self->lonL;
	copy->latB  = self->latB;
	copy->lonR  = self->lonR;

	// success
	return copy;

	// failure
	fail_abrev:
		free(copy->name);
	fail_name:
		cc_list_delete(&copy->members);
	fail_members:
		free(copy);
	return NULL;
}

void osmdb_relation_delete(osmdb_relation_t** _self)
{
	assert(_self);

	osmdb_relation_t* self = *_self;
	if(self)
	{
		osmdb_relation_discardMembers(self);
		cc_list_delete(&self->members);
		free(self->name);
		free(self->abrev);
		free(self);
		*_self = NULL;
	}
}

void osmdb_relation_incref(osmdb_relation_t* self)
{
	assert(self);

	++self->refcount;
}

int osmdb_relation_decref(osmdb_relation_t* self)
{
	assert(self);

	--self->refcount;
	return (self->refcount == 0) ? 1 : 0;
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
	if((self->latT == 0.0) && (self->lonL == 0.0) &&
	   (self->latB == 0.0) && (self->lonR == 0.0))
	{
		// skip range
	}
	else
	{
		ret &= xml_ostream_attrf(os, "latT", "%lf", self->latT);
		ret &= xml_ostream_attrf(os, "lonL", "%lf", self->lonL);
		ret &= xml_ostream_attrf(os, "latB", "%lf", self->latB);
		ret &= xml_ostream_attrf(os, "lonR", "%lf", self->lonR);
	}

	cc_listIter_t* iter = cc_list_head(self->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    cc_list_peekIter(iter);
		ret &= xml_ostream_begin(os, "member");
		ret &= xml_ostream_attr(os, "type",
		                        osmdb_relationMemberCodeToType(m->type));
		ret &= xml_ostream_attrf(os, "ref", "%0.0lf", m->ref);
		if(m->role)
		{
			ret &= xml_ostream_attr(os, "role",
			                        osmdb_relationMemberCodeToRole(m->role));
		}
		ret &= xml_ostream_end(os);
		iter = cc_list_next(iter);
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
	size += sizeof(osmdb_member_t)*cc_list_size(self->members);

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

	// optional atts
	if(role)
	{
		member->role = osmdb_relationMemberRoleToCode(role);
	}

	// add member to list
	if(cc_list_append(self->members, NULL,
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

void osmdb_relation_updateRange(osmdb_relation_t* self,
                                osmdb_range_t* range)
{
	assert(self);
	assert(range);

	self->latT = range->latT;
	self->lonL = range->lonL;
	self->latB = range->latB;
	self->lonR = range->lonR;
}

int osmdb_relation_copyMember(osmdb_relation_t* self,
                              osmdb_member_t* member)
{
	assert(self);
	assert(member);

	osmdb_member_t* m = (osmdb_member_t*)
	                    malloc(sizeof(osmdb_member_t));
	if(m == NULL)
	{
		LOGE("malloc failed");
		return 0;
	}

	m->type = member->type;
	m->ref  = member->ref;
	m->role = member->role;

	// add member to list
	if(cc_list_append(self->members, NULL,
	                  (const void*) m) == NULL)
	{
		goto fail_append;
	}

	// succcess
	return 1;

	// failure
	fail_append:
		free(m);
	return 0;
}

void osmdb_relation_discardMembers(osmdb_relation_t* self)
{
	assert(self);

	cc_listIter_t* iter = cc_list_head(self->members);
	while(iter)
	{
		osmdb_member_t* m;
		m = (osmdb_member_t*)
		    cc_list_remove(self->members, &iter);
		free(m);
	}
}

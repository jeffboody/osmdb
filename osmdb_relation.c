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
#include <string.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "../libcc/cc_memory.h"
#include "osmdb_relation.h"
#include "osmdb_util.h"

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_relation_t*
osmdb_relation_new(double id, const char* name,
                   const char* abrev, int class,
                   double latT, double lonL,
                   double latB, double lonR)
{
	// name and abrev may be NULL

	osmdb_relation_t* self;
	self = (osmdb_relation_t*)
	       CALLOC(1, sizeof(osmdb_relation_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->base.type = OSMDB_OBJECT_TYPE_RELATION;
	self->base.id   = id;
	self->class     = class;
	self->latT      = latT;
	self->lonL      = lonL;
	self->latB      = latB;
	self->lonR      = lonR;

	if(name && (name[0] != '\0'))
	{
		int len = strlen(name) + 1;
		self->name = (char*) MALLOC(len*sizeof(char));
		if(self->name == NULL)
		{
			LOGE("MALLOC failed");
			goto fail_name;
		}
		snprintf(self->name, len, "%s", name);
	}

	if(abrev && (abrev[0] != '\0'))
	{
		int len = strlen(abrev) + 1;
		self->abrev = (char*) MALLOC(len*sizeof(char));
		if(self->abrev == NULL)
		{
			LOGE("MALLOC failed");
			goto fail_abrev;
		}
		snprintf(self->abrev, len, "%s", abrev);
	}

	self->members = cc_list_new();
	if(self->members == NULL)
	{
		goto fail_members;
	}

	// success
	return self;

	// failure
	fail_members:
		FREE(self->abrev);
	fail_abrev:
		FREE(self->name);
	fail_name:
		FREE(self);
	return NULL;
}

osmdb_relation_t*
osmdb_relation_newXml(const char** atts, int line)
{
	ASSERT(atts);

	const char* att_id    = NULL;
	const char* att_name  = NULL;
	const char* att_abrev = NULL;
	const char* att_class = NULL;
	const char* att_latT  = NULL;
	const char* att_lonL  = NULL;
	const char* att_latB  = NULL;
	const char* att_lonR  = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "id") == 0)
		{
			att_id = atts[idx1];
		}
		else if(strcmp(atts[idx0], "name") == 0)
		{
			att_name = atts[idx1];
		}
		else if(strcmp(atts[idx0], "abrev") == 0)
		{
			att_abrev = atts[idx1];
		}
		else if(strcmp(atts[idx0], "class") == 0)
		{
			att_class = atts[idx1];
		}
		else if(strcmp(atts[idx0], "latT") == 0)
		{
			att_latT = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lonL") == 0)
		{
			att_lonL = atts[idx1];
		}
		else if(strcmp(atts[idx0], "latB") == 0)
		{
			att_latB = atts[idx1];
		}
		else if(strcmp(atts[idx0], "lonR") == 0)
		{
			att_lonR = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if(att_id == NULL)
	{
		LOGE("invalid line=%i", line);
		return NULL;
	}

	double id = strtod(att_id, NULL);

	int class = 0;
	if(att_class)
	{
		class = osmdb_classNameToCode(att_class);
	}

	double latT = 0.0;
	if(att_latT)
	{
		latT = strtod(att_latT, NULL);
	}

	double lonL = 0.0;
	if(att_lonL)
	{
		lonL = strtod(att_lonL, NULL);
	}

	double latB = 0.0;
	if(att_latB)
	{
		latB = strtod(att_latB, NULL);
	}

	double lonR = 0.0;
	if(att_lonR)
	{
		lonR = strtod(att_lonR, NULL);
	}

	return osmdb_relation_new(id, att_name, att_abrev, class,
	                          latT, lonL, latB, lonR);
}

int osmdb_relation_newMember(osmdb_relation_t* self,
                             int type, double ref,
                             int role)
{
	ASSERT(self);

	osmdb_member_t* member;
	member = (osmdb_member_t*)
	         CALLOC(1, sizeof(osmdb_member_t));
	if(member == NULL)
	{
		LOGE("CALLOC failed");
		return 0;
	}

	member->type = type;
	member->ref  = ref;
	member->role = role;

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
		FREE(member);
	return 0;
}

int osmdb_relation_newMemberXml(osmdb_relation_t* self,
                                const char** atts, int line)
{
	ASSERT(self);
	ASSERT(atts);

	const char* att_type = NULL;
	const char* att_ref  = NULL;
	const char* att_role = NULL;

	// find atts
	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "type") == 0)
		{
			att_type = atts[idx1];
		}
		else if(strcmp(atts[idx0], "ref") == 0)
		{
			att_ref = atts[idx1];
		}
		else if(strcmp(atts[idx0], "role") == 0)
		{
			att_role = atts[idx1];
		}
		idx0 += 2;
		idx1 += 2;
	}

	// check for required atts
	if((att_type == NULL) || (att_ref == NULL))
	{
		LOGE("invalid line=%i", line);
		return 0;
	}

	int    type = osmdb_relationMemberTypeToCode(att_type);
	double ref  = strtod(att_ref, NULL);
	int    role = 0;

	// optional atts
	if(att_role)
	{
		role = osmdb_relationMemberRoleToCode(att_role);
	}

	return osmdb_relation_newMember(self, type, ref, role);
}

void osmdb_relation_delete(osmdb_relation_t** _self)
{
	ASSERT(_self);

	osmdb_relation_t* self = *_self;
	if(self)
	{
		osmdb_relation_discardMembers(self);
		cc_list_delete(&self->members);
		FREE(self->name);
		FREE(self->abrev);
		FREE(self);
		*_self = NULL;
	}
}

osmdb_relation_t*
osmdb_relation_copy(osmdb_relation_t* self)
{
	ASSERT(self);

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
	ASSERT(self);

	osmdb_relation_t* copy;
	copy = (osmdb_relation_t*)
	       CALLOC(1, sizeof(osmdb_relation_t));
	if(copy == NULL)
	{
		LOGE("CALLOC failed");
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
		copy->name = (char*) MALLOC(len*sizeof(char));
		if(copy->name == NULL)
		{
			LOGE("MALLOC failed");
			goto fail_name;
		}
		snprintf(copy->name, len, "%s", self->name);
	}

	if(self->abrev)
	{
		int len = strlen(self->abrev) + 1;
		copy->abrev = (char*) MALLOC(len*sizeof(char));
		if(copy->abrev == NULL)
		{
			LOGE("MALLOC failed");
			goto fail_abrev;
		}
		snprintf(copy->abrev, len, "%s", self->abrev);
	}

	copy->base.id = self->base.id;
	copy->class   = self->class;
	copy->latT    = self->latT;
	copy->lonL    = self->lonL;
	copy->latB    = self->latB;
	copy->lonR    = self->lonR;

	// success
	return copy;

	// failure
	fail_abrev:
		FREE(copy->name);
	fail_name:
		cc_list_delete(&copy->members);
	fail_members:
		FREE(copy);
	return NULL;
}

int osmdb_relation_copyMember(osmdb_relation_t* self,
                              osmdb_member_t* member)
{
	ASSERT(self);
	ASSERT(member);

	osmdb_member_t* m;
	m = (osmdb_member_t*)
	    MALLOC(sizeof(osmdb_member_t));
	if(m == NULL)
	{
		LOGE("MALLOC failed");
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
		FREE(m);
	return 0;
}

void osmdb_relation_incref(osmdb_relation_t* self)
{
	ASSERT(self);

	++self->base.refcount;
}

int osmdb_relation_decref(osmdb_relation_t* self)
{
	ASSERT(self);

	--self->base.refcount;
	return (self->base.refcount == 0) ? 1 : 0;
}

int osmdb_relation_export(osmdb_relation_t* self,
                          xml_ostream_t* os)
{
	ASSERT(self);
	ASSERT(os);

	int ret = 1;
	ret &= xml_ostream_begin(os, "relation");
	ret &= xml_ostream_attrf(os, "id", "%0.0lf", self->base.id);
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
	ASSERT(self);

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

void osmdb_relation_updateRange(osmdb_relation_t* self,
                                osmdb_range_t* range)
{
	ASSERT(self);
	ASSERT(range);

	self->latT = range->latT;
	self->lonL = range->lonL;
	self->latB = range->latB;
	self->lonR = range->lonR;
}

void osmdb_relation_discardMembers(osmdb_relation_t* self)
{
	ASSERT(self);

	cc_listIter_t* iter = cc_list_head(self->members);
	while(iter)
	{
		osmdb_member_t* m;
		m = (osmdb_member_t*)
		    cc_list_remove(self->members, &iter);
		FREE(m);
	}
}

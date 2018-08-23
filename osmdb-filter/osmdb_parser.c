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
#include "libxmlstream/xml_istream.h"
#include "osmdb_parser.h"
#include "../osmdb_util.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

#define OSMDB_STATE_INIT              0
#define OSMDB_STATE_OSMDB             1
#define OSMDB_STATE_OSMDB_NODE        2
#define OSMDB_STATE_OSMDB_WAY         3
#define OSMDB_STATE_OSMDB_WAY_ND      4
#define OSMDB_STATE_OSMDB_REL         5
#define OSMDB_STATE_OSMDB_REL_MEMBER  6
#define OSMDB_STATE_DONE             -1

/***********************************************************
* private                                                  *
***********************************************************/

static int
osmdb_parser_select(osmdb_parser_t* self,
                    a3d_hashmap_t* hash,
                    const char** atts)
{
	assert(self);
	assert(hash);
	assert(atts);

	int         idx0  = 0;
	int         idx1  = 1;
	const char* id    = NULL;
	const char* class = NULL;
	const char* name  = NULL;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "id") == 0)
		{
			id = atts[idx1];
		}
		else if(strcmp(atts[idx0], "class") == 0)
		{
			class = atts[idx1];
		}
		else if(strcmp(atts[idx0], "name") == 0)
		{
			name = atts[idx1];
		}

		idx0 += 2;
		idx1 += 2;
	}

	// validate id
	if(id == NULL)
	{
		return 0;
	}

	// validate name
	const char empty[256] = "";
	if(name == NULL)
	{
		name = empty;
	}

	a3d_hashmapIter_t iter;
	if(a3d_hashmap_find(hash, &iter, id))
	{
		return 1;
	}
	else if(class &&
		    a3d_hashmap_find(self->classes, &iter, class))
	{
		// copy name
		int len = strlen(name) + 1;
		char* s = (char*) malloc(len*sizeof(char));
		if(s == NULL)
		{
			LOGE("malloc failed");
			return 0;
		}
		snprintf(s, len, "%s", name);

		// add id->name
		a3d_hashmapIter_t iter;
		if(a3d_hashmap_add(hash, &iter, s, id) == 0)
		{
			LOGE("a3d_hashmap_add failed id=%s", id);
			free(s);
			return 0;
		}

		return 1;
	}

	return 0;
}

static int
osmdb_parser_beginOsm(osmdb_parser_t* self, int line,
                      const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB;
	if(self->mode == OSMDB_MODE_WRITE)
	{
		xml_ostream_begin(self->os, "osmdb");
	}

	return 1;
}

static int
osmdb_parser_endOsm(osmdb_parser_t* self, int line,
                    const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_DONE;
	if(self->mode == OSMDB_MODE_WRITE)
	{
		xml_ostream_end(self->os);
	}

	return 1;
}

static int
osmdb_parser_beginOsmNode(osmdb_parser_t* self, int line,
                          const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_NODE;

	if((self->mode == OSMDB_MODE_NODE) ||
	   (self->mode == OSMDB_MODE_WRITE))
	{
		self->selected = osmdb_parser_select(self,
		                                     self->nodes,
		                                     atts);
	}

	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_begin(self->os, "node");

		int idx0 = 0;
		int idx1 = 1;
		while(atts[idx0] && atts[idx1])
		{
			xml_ostream_attr(self->os, atts[idx0], atts[idx1]);
			idx0 += 2;
			idx1 += 2;
		}
	}

	return 1;
}

static int
osmdb_parser_endOsmNode(osmdb_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB;
	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_end(self->os);
		self->selected = 0;
	}

	return 1;
}

static int
osmdb_parser_beginOsmWay(osmdb_parser_t* self, int line,
                         const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_WAY;

	if((self->mode == OSMDB_MODE_WAY) ||
	   (self->mode == OSMDB_MODE_WRITE))
	{
		self->selected = osmdb_parser_select(self,
		                                     self->ways,
		                                     atts);
	}

	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_begin(self->os, "way");

		int idx0 = 0;
		int idx1 = 1;
		while(atts[idx0] && atts[idx1])
		{
			xml_ostream_attr(self->os, atts[idx0], atts[idx1]);
			idx0 += 2;
			idx1 += 2;
		}
	}

	return 1;
}

static int
osmdb_parser_endOsmWay(osmdb_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB;
	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_end(self->os);
		self->selected = 0;
	}

	return 1;
}

static int
osmdb_parser_beginOsmWayNd(osmdb_parser_t* self, int line,
                           const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_WAY_ND;

	if(self->selected)
	{
		if(self->mode == OSMDB_MODE_WRITE)
		{
			xml_ostream_begin(self->os, "nd");
		}

		int idx0 = 0;
		int idx1 = 1;
		const char* ref = NULL;
		while(atts[idx0] && atts[idx1])
		{
			if(strcmp(atts[idx0], "ref") == 0)
			{
				ref = atts[idx1];
			}

			if(self->mode == OSMDB_MODE_WRITE)
			{
				xml_ostream_attr(self->os, atts[idx0], atts[idx1]);
			}
			idx0 += 2;
			idx1 += 2;
		}

		if(ref)
		{
			// check if ref already exists
			a3d_hashmapIter_t iter;
			if(a3d_hashmap_find(self->nodes, &iter, ref))
			{
				return 1;
			}

			int len = strlen(ref) + 1;
			char* s = (char*) malloc(len*sizeof(char));
			if(s == NULL)
			{
				LOGE("malloc failed");
				return 0;
			}
			snprintf(s, len, "%s", ref);

			// add ref->ref
			if(a3d_hashmap_add(self->nodes, &iter, s, s) == 0)
			{
				LOGE("a3d_hashmap_add failed ref=%s", s);
				free(s);
				return 0;
			}
		}
	}

	return 1;
}

static int
osmdb_parser_endOsmWayNd(osmdb_parser_t* self, int line,
                         const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB_WAY;
	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_end(self->os);
	}

	return 1;
}

static int
osmdb_parser_beginOsmRel(osmdb_parser_t* self, int line,
                         const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_REL;

	if((self->mode == OSMDB_MODE_REL) ||
	   (self->mode == OSMDB_MODE_WRITE))
	{
		self->selected = osmdb_parser_select(self,
		                                     self->rels,
		                                     atts);
	}

	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_begin(self->os, "relation");

		int idx0 = 0;
		int idx1 = 1;
		while(atts[idx0] && atts[idx1])
		{
			xml_ostream_attr(self->os, atts[idx0], atts[idx1]);
			idx0 += 2;
			idx1 += 2;
		}
	}

	return 1;
}

static int
osmdb_parser_endOsmRel(osmdb_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB;
	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_end(self->os);
		self->selected = 0;
	}

	return 1;
}

static int
osmdb_parser_beginOsmRelMember(osmdb_parser_t* self, int line,
                               const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_REL_MEMBER;

	if(self->selected)
	{
		if(self->mode == OSMDB_MODE_WRITE)
		{
			xml_ostream_begin(self->os, "member");
		}

		int idx0 = 0;
		int idx1 = 1;
		const char* type = NULL;
		const char* ref  = NULL;
		while(atts[idx0] && atts[idx1])
		{
			if(strcmp(atts[idx0], "ref") == 0)
			{
				ref = atts[idx1];
			}
			else if(strcmp(atts[idx0], "type") == 0)
			{
				type = atts[idx1];
			}

			if(self->mode == OSMDB_MODE_WRITE)
			{
				xml_ostream_attr(self->os, atts[idx0], atts[idx1]);
			}
			idx0 += 2;
			idx1 += 2;
		}

		if(ref && type)
		{
			a3d_hashmap_t* hash = NULL;
			if(strcmp(type, "node") == 0)
			{
				hash = self->nodes;
			}
			else if(strcmp(type, "way") == 0)
			{
				hash = self->ways;
			}
			else
			{
				LOGW("invalid type=%s, ref=%s, line=%i",
				     type, ref, line);
				return 1;
			}

			// check if ref already exists
			a3d_hashmapIter_t iter;
			if(a3d_hashmap_find(hash, &iter, ref))
			{
				return 1;
			}

			// add ref->ref
			int len = strlen(ref) + 1;
			char* s = (char*) malloc(len*sizeof(char));
			if(s == NULL)
			{
				LOGE("malloc failed");
				return 0;
			}
			snprintf(s, len, "%s", ref);

			if(a3d_hashmap_add(hash, &iter, s, s) == 0)
			{
				LOGE("a3d_hashmap_add failed ref=%s", s);
				free(s);
				return 0;
			}
		}
	}

	return 1;
}

static int
osmdb_parser_endOsmRelMember(osmdb_parser_t* self, int line,
                             const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB_REL;
	if(self->selected && (self->mode == OSMDB_MODE_WRITE))
	{
		xml_ostream_end(self->os);
	}

	return 1;
}

/***********************************************************
* public                                                   *
***********************************************************/

osmdb_parser_t* osmdb_parser_new(a3d_hashmap_t* classes,
                                 const char* gzname)
{
	assert(classes);
	assert(gzname);

	osmdb_parser_t* self = (osmdb_parser_t*)
	                       calloc(1, sizeof(osmdb_parser_t));
	if(self == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	self->nodes = a3d_hashmap_new();
	if(self->nodes == NULL)
	{
		goto fail_nodes;
	}

	self->ways = a3d_hashmap_new();
	if(self->ways == NULL)
	{
		goto fail_ways;
	}

	self->rels = a3d_hashmap_new();
	if(self->rels == NULL)
	{
		goto fail_rels;
	}

	self->os = xml_ostream_newGz(gzname);
	if(self->os == NULL)
	{
		goto fail_os;
	}

	self->classes = classes;

	// success
	return self;

	// failure
	fail_os:
		a3d_hashmap_delete(&self->rels);
	fail_rels:
		a3d_hashmap_delete(&self->ways);
	fail_ways:
		a3d_hashmap_delete(&self->nodes);
	fail_nodes:
		free(self);
	return NULL;
}

void osmdb_parser_delete(osmdb_parser_t** _self)
{
	assert(_self);

	osmdb_parser_t* self = *_self;
	if(self)
	{
		a3d_hashmapIter_t  iterator;
		a3d_hashmapIter_t* iter;
		iter = a3d_hashmap_head(self->nodes, &iterator);
		while(iter)
		{
			char* s = (char*) a3d_hashmap_remove(self->nodes,
			                                     &iter);
			free(s);
		}

		iter = a3d_hashmap_head(self->ways, &iterator);
		while(iter)
		{
			char* s = (char*) a3d_hashmap_remove(self->ways,
			                                     &iter);
			free(s);
		}

		iter = a3d_hashmap_head(self->rels, &iterator);
		while(iter)
		{
			char* s = (char*) a3d_hashmap_remove(self->rels,
			                                     &iter);
			free(s);
		}

		xml_ostream_delete(&self->os);

		free(self);
		*_self = NULL;
	}
}

void osmdb_parser_mode(osmdb_parser_t* self, int mode)
{
	assert(self);

	self->state = OSMDB_STATE_INIT;
	self->mode  = mode;
}

int osmdb_parser_start(void* priv,
                       int line,
                       const char* name,
                       const char** atts)
{
	assert(priv);
	assert(name);
	assert(atts);

	osmdb_parser_t* self = (osmdb_parser_t*) priv;

	int state = self->state;
	if(state == OSMDB_STATE_INIT)
	{
		if(strcmp(name, "osmdb") == 0)
		{
			return osmdb_parser_beginOsm(self, line, atts);
		}
	}
	else if(state == OSMDB_STATE_OSMDB)
	{
		if(strcmp(name, "node") == 0)
		{
			return osmdb_parser_beginOsmNode(self, line, atts);
		}
		else if(strcmp(name, "way") == 0)
		{
			return osmdb_parser_beginOsmWay(self, line, atts);
		}
		else if(strcmp(name, "relation") == 0)
		{
			return osmdb_parser_beginOsmRel(self, line, atts);
		}
	}
	else if(state == OSMDB_STATE_OSMDB_WAY)
	{
		if(strcmp(name, "nd") == 0)
		{
			return osmdb_parser_beginOsmWayNd(self, line, atts);
		}
	}
	else if(state == OSMDB_STATE_OSMDB_REL)
	{
		if(strcmp(name, "member") == 0)
		{
			return osmdb_parser_beginOsmRelMember(self, line, atts);
		}
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

int osmdb_parser_end(void* priv,
                     int line,
                     const char* name,
                     const char* content)
{
	// content may be NULL
	assert(priv);
	assert(name);

	osmdb_parser_t* self = (osmdb_parser_t*) priv;

	int state = self->state;
	if(state == OSMDB_STATE_OSMDB)
	{
		return osmdb_parser_endOsm(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_NODE)
	{
		return osmdb_parser_endOsmNode(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_WAY)
	{
		return osmdb_parser_endOsmWay(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_REL)
	{
		return osmdb_parser_endOsmRel(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_WAY_ND)
	{
		return osmdb_parser_endOsmWayNd(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_REL_MEMBER)
	{
		return osmdb_parser_endOsmRelMember(self, line, content);
	}

	LOGE("state=%i, name=%s, line=%i",
	     state, name, line);
	return 0;
}

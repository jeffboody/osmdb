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
#include "libxmlstream/xml_istream.h"
#include "osmdb_parser.h"

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

typedef struct
{
	int state;

	// callbacks
	void* priv;
	osmdb_parser_nodeFn     node_fn;
	osmdb_parser_wayFn      way_fn;
	osmdb_parser_relationFn relation_fn;

	// temporary data
	union
	{
		osmdb_node_t*     node;
		osmdb_way_t*      way;
		osmdb_relation_t* relation;
	};
} osmdb_parser_t;

static int
osmdb_parser_beginOsm(osmdb_parser_t* self, int line,
                      const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB;

	return 1;
}

static int
osmdb_parser_endOsm(osmdb_parser_t* self, int line,
                    const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_DONE;

	return 1;
}

static int
osmdb_parser_beginOsmNode(osmdb_parser_t* self, int line,
                          const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_NODE;
	self->node  = osmdb_node_new(atts, line);
	return self->node ? 1 : 0;
}

static int
osmdb_parser_endOsmNode(osmdb_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB;
	if(self->node == NULL)
	{
		return 0;
	}
	else if(self->node_fn(self->priv, self->node) == 0)
	{
		osmdb_node_delete(&self->node);
		return 0;
	}

	self->node = NULL;
	return 1;
}

static int
osmdb_parser_beginOsmWay(osmdb_parser_t* self, int line,
                         const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_WAY;
	self->way   = osmdb_way_new(atts, line);
	return self->way ? 1 : 0;
}

static int
osmdb_parser_endOsmWay(osmdb_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB;
	if(self->way == NULL)
	{
		return 0;
	}
	else if(self->way_fn(self->priv, self->way) == 0)
	{
		osmdb_way_delete(&self->way);
		return 0;
	}

	self->way = NULL;
	return 1;
}

static int
osmdb_parser_beginOsmWayNd(osmdb_parser_t* self, int line,
                           const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_WAY_ND;
	if(self->way == NULL)
	{
		return 0;
	}
	else if(osmdb_way_nd(self->way, atts, line) == 0)
	{
		osmdb_way_delete(&self->way);
		return 0;
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
	return 1;
}

static int
osmdb_parser_beginOsmRel(osmdb_parser_t* self, int line,
                         const char** atts)
{
	assert(self);
	assert(atts);

	self->state    = OSMDB_STATE_OSMDB_REL;
	self->relation = osmdb_relation_new(atts, line);
	return self->relation ? 1 : 0;
}

static int
osmdb_parser_endOsmRel(osmdb_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	assert(self);

	self->state = OSMDB_STATE_OSMDB;
	if(self->relation == NULL)
	{
		return 0;
	}
	else if(self->relation_fn(self->priv, self->relation) == 0)
	{
		osmdb_relation_delete(&self->relation);
		return 0;
	}

	self->relation = NULL;
	return 1;
}

static int
osmdb_parser_beginOsmRelMember(osmdb_parser_t* self, int line,
                               const char** atts)
{
	assert(self);
	assert(atts);

	self->state = OSMDB_STATE_OSMDB_REL_MEMBER;
	if(self->relation == NULL)
	{
		return 0;
	}
	else if(osmdb_relation_member(self->relation, atts, line) == 0)
	{
		osmdb_relation_delete(&self->relation);
		return 0;
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
	return 1;
}

static osmdb_parser_t*
osmdb_parser_new(void* priv,
                 osmdb_parser_nodeFn node_fn,
                 osmdb_parser_wayFn way_fn,
                 osmdb_parser_relationFn relation_fn)
{
	assert(node_fn);
	assert(way_fn);
	assert(relation_fn);

	osmdb_parser_t* self = (osmdb_parser_t*)
	                       calloc(1, sizeof(osmdb_parser_t));
	if(self == NULL)
	{
		LOGE("calloc failed");
		return NULL;
	}

	self->priv        = priv;
	self->node_fn     = node_fn;
	self->way_fn      = way_fn;
	self->relation_fn = relation_fn;

	return self;
}

static void osmdb_parser_delete(osmdb_parser_t** _self)
{
	assert(_self);

	osmdb_parser_t* self = *_self;
	if(self)
	{
		if(self->state == OSMDB_STATE_OSMDB_NODE)
		{
			osmdb_node_delete(&self->node);
		}
		else if((self->state == OSMDB_STATE_OSMDB_WAY) ||
		        (self->state == OSMDB_STATE_OSMDB_WAY_ND))
		{
			osmdb_way_delete(&self->way);
		}
		else if((self->state == OSMDB_STATE_OSMDB_REL) ||
		        (self->state == OSMDB_STATE_OSMDB_REL_MEMBER))
		{
			osmdb_relation_delete(&self->relation);
		}

		free(self);
		*_self = NULL;
	}
}

static int osmdb_parser_start(void* priv,
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

static int osmdb_parser_end(void* priv,
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

/***********************************************************
* public                                                   *
***********************************************************/

int osmdb_parse(const char* fname, void* priv,
                osmdb_parser_nodeFn node_fn,
                osmdb_parser_wayFn way_fn,
                osmdb_parser_relationFn relation_fn)
{
	// priv may be NULL
	assert(fname);
	assert(node_fn);
	assert(way_fn);
	assert(relation_fn);

	osmdb_parser_t* self;
	self = osmdb_parser_new(priv, node_fn, way_fn, relation_fn);
	if(self == NULL)
	{
		return NULL;
	}

	if(xml_istream_parseGz((void*) self,
	                       osmdb_parser_start,
	                       osmdb_parser_end,
	                       fname) == 0)
	{
		goto fail_parse;
	}

	osmdb_parser_delete(&self);

	// success
	return 1;

	// failure
	fail_parse:
		osmdb_parser_delete(&self);
	return 0;
}

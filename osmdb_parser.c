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
#include "../libxmlstream/xml_istream.h"
#include "osmdb_parser.h"

#define OSMDB_STATE_INIT              0
#define OSMDB_STATE_OSMDB             1
#define OSMDB_STATE_OSMDB_NODE        2
#define OSMDB_STATE_OSMDB_WAY         3
#define OSMDB_STATE_OSMDB_WAY_ND      4
#define OSMDB_STATE_OSMDB_REL         5
#define OSMDB_STATE_OSMDB_REL_MEMBER  6
#define OSMDB_STATE_OSMDB_NODE_REF    7
#define OSMDB_STATE_OSMDB_WAY_REF     8
#define OSMDB_STATE_OSMDB_REL_REF     9
#define OSMDB_STATE_DONE             -1

/***********************************************************
* private                                                  *
***********************************************************/

typedef struct
{
	int state;

	// callbacks
	void* priv;
	osmdb_parser_nodeFn        node_fn;
	osmdb_parser_wayFn         way_fn;
	osmdb_parser_relationFn    relation_fn;
	osmdb_parser_nodeRefFn     nref_fn;
	osmdb_parser_wayRefFn      wref_fn;
	osmdb_parser_relationRefFn rref_fn;

	// temporary data
	union
	{
		osmdb_node_t*     node;
		osmdb_way_t*      way;
		osmdb_relation_t* relation;
	};
} osmdb_parser_t;

static int
osmdb_parser_defaultNodeFn(void* priv, osmdb_node_t* node)
{
	ASSERT(priv);
	ASSERT(node);

	return 0;
}

static int
osmdb_parser_defaultWayFn(void* priv, osmdb_way_t* way)
{
	ASSERT(priv);
	ASSERT(way);

	return 0;
}

static int
osmdb_parser_defaultRelationFn(void* priv,
                               osmdb_relation_t* relation)
{
	ASSERT(priv);
	ASSERT(relation);

	return 0;
}

static int
osmdb_parser_defaultRefFn(void* priv, double ref)
{
	ASSERT(priv);

	return 0;
}

static int
osmdb_parser_beginOsm(osmdb_parser_t* self, int line,
                      const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB;

	return 1;
}

static int
osmdb_parser_endOsm(osmdb_parser_t* self, int line,
                    const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSMDB_STATE_DONE;

	return 1;
}

static int
osmdb_parser_beginOsmNode(osmdb_parser_t* self, int line,
                          const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_NODE;
	self->node  = osmdb_node_newXml(atts, line);
	return self->node ? 1 : 0;
}

static int
osmdb_parser_endOsmNode(osmdb_parser_t* self, int line,
                        const char* content)
{
	// content may be NULL
	ASSERT(self);

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
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_WAY;
	self->way   = osmdb_way_newXml(atts, line);
	return self->way ? 1 : 0;
}

static int
osmdb_parser_endOsmWay(osmdb_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	ASSERT(self);

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
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_WAY_ND;
	if(self->way == NULL)
	{
		return 0;
	}
	else if(osmdb_way_newNdXml(self->way, atts, line) == 0)
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
	ASSERT(self);

	self->state = OSMDB_STATE_OSMDB_WAY;
	return 1;
}

static int
osmdb_parser_beginOsmRel(osmdb_parser_t* self, int line,
                         const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state    = OSMDB_STATE_OSMDB_REL;
	self->relation = osmdb_relation_newXml(atts, line);
	return self->relation ? 1 : 0;
}

static int
osmdb_parser_endOsmRel(osmdb_parser_t* self, int line,
                       const char* content)
{
	// content may be NULL
	ASSERT(self);

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
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_REL_MEMBER;
	if(self->relation == NULL)
	{
		return 0;
	}
	else if(osmdb_relation_newMemberXml(self->relation, atts,
	                                    line) == 0)
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
	ASSERT(self);

	self->state = OSMDB_STATE_OSMDB_REL;
	return 1;
}

static int
osmdb_parser_beginOsmNodeRef(osmdb_parser_t* self, int line,
                             const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_NODE_REF;

	const char* ref = NULL;

	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "ref") == 0)
		{
			ref = atts[idx1];
			break;
		}

		idx0 += 2;
		idx1 += 2;
	}

	if(ref == NULL)
	{
		return 0;
	}

	return self->nref_fn(self->priv, strtod(ref, NULL));
}

static int
osmdb_parser_endOsmNodeRef(osmdb_parser_t* self, int line,
                           const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSMDB_STATE_OSMDB;
	return 1;
}

static int
osmdb_parser_beginOsmWayRef(osmdb_parser_t* self, int line,
                            const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_WAY_REF;

	const char* ref = NULL;

	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "ref") == 0)
		{
			ref = atts[idx1];
			break;
		}

		idx0 += 2;
		idx1 += 2;
	}

	if(ref == NULL)
	{
		return 0;
	}

	return self->wref_fn(self->priv, strtod(ref, NULL));
}

static int
osmdb_parser_endOsmWayRef(osmdb_parser_t* self, int line,
                          const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSMDB_STATE_OSMDB;
	return 1;
}

static int
osmdb_parser_beginOsmRelationRef(osmdb_parser_t* self, int line,
                                 const char** atts)
{
	ASSERT(self);
	ASSERT(atts);

	self->state = OSMDB_STATE_OSMDB_REL_REF;

	const char* ref = NULL;

	int idx0 = 0;
	int idx1 = 1;
	while(atts[idx0] && atts[idx1])
	{
		if(strcmp(atts[idx0], "ref") == 0)
		{
			ref = atts[idx1];
			break;
		}

		idx0 += 2;
		idx1 += 2;
	}

	if(ref == NULL)
	{
		return 0;
	}

	return self->rref_fn(self->priv, strtod(ref, NULL));
}

static int
osmdb_parser_endOsmRelationRef(osmdb_parser_t* self, int line,
                               const char* content)
{
	// content may be NULL
	ASSERT(self);

	self->state = OSMDB_STATE_OSMDB;
	return 1;
}

static osmdb_parser_t*
osmdb_parser_new(void* priv,
                 osmdb_parser_nodeFn node_fn,
                 osmdb_parser_wayFn way_fn,
                 osmdb_parser_relationFn relation_fn,
                 osmdb_parser_nodeRefFn nref_fn,
                 osmdb_parser_wayRefFn wref_fn,
                 osmdb_parser_relationRefFn rref_fn)
{
	ASSERT(node_fn);
	ASSERT(way_fn);
	ASSERT(relation_fn);
	ASSERT(nref_fn);
	ASSERT(wref_fn);
	ASSERT(rref_fn);

	osmdb_parser_t* self;
	self = (osmdb_parser_t*) CALLOC(1, sizeof(osmdb_parser_t));
	if(self == NULL)
	{
		LOGE("CALLOC failed");
		return NULL;
	}

	self->priv        = priv;
	self->node_fn     = node_fn;
	self->way_fn      = way_fn;
	self->relation_fn = relation_fn;
	self->nref_fn     = nref_fn;
	self->wref_fn     = wref_fn;
	self->rref_fn     = rref_fn;

	return self;
}

static void osmdb_parser_delete(osmdb_parser_t** _self)
{
	ASSERT(_self);

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

		FREE(self);
		*_self = NULL;
	}
}

static int osmdb_parser_start(void* priv,
                              int line,
                              const char* name,
                              const char** atts)
{
	ASSERT(priv);
	ASSERT(name);
	ASSERT(atts);

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
		else if(strcmp(name, "n") == 0)
		{
			return osmdb_parser_beginOsmNodeRef(self, line, atts);
		}
		else if(strcmp(name, "w") == 0)
		{
			return osmdb_parser_beginOsmWayRef(self, line, atts);
		}
		else if(strcmp(name, "r") == 0)
		{
			return osmdb_parser_beginOsmRelationRef(self, line, atts);
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
	ASSERT(priv);
	ASSERT(name);

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
	else if(state == OSMDB_STATE_OSMDB_NODE_REF)
	{
		return osmdb_parser_endOsmNodeRef(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_WAY_REF)
	{
		return osmdb_parser_endOsmWayRef(self, line, content);
	}
	else if(state == OSMDB_STATE_OSMDB_REL_REF)
	{
		return osmdb_parser_endOsmRelationRef(self, line, content);
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
	ASSERT(fname);
	ASSERT(node_fn);
	ASSERT(way_fn);
	ASSERT(relation_fn);

	osmdb_parser_t* self;
	self = osmdb_parser_new(priv, node_fn, way_fn, relation_fn,
	                        osmdb_parser_defaultRefFn,
	                        osmdb_parser_defaultRefFn,
	                        osmdb_parser_defaultRefFn);
	if(self == NULL)
	{
		return 0;
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

int osmdb_parseRefs(const char* fname, void* priv,
                    osmdb_parser_nodeRefFn nref_fn,
                    osmdb_parser_wayRefFn wref_fn,
                    osmdb_parser_relationRefFn rref_fn)
{
	// priv may be NULL
	ASSERT(fname);
	ASSERT(nref_fn);
	ASSERT(wref_fn);
	ASSERT(rref_fn);

	osmdb_parser_t* self;
	self = osmdb_parser_new(priv,
	                        osmdb_parser_defaultNodeFn,
	                        osmdb_parser_defaultWayFn,
	                        osmdb_parser_defaultRelationFn,
	                        nref_fn, wref_fn, rref_fn);
	if(self == NULL)
	{
		return 0;
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

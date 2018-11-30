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

#include "osmdb_range.h"
#include "osmdb_index.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* public                                                   *
***********************************************************/

void osmdb_range_init(osmdb_range_t* self)
{
	assert(self);

	self->pts  = 0;
	self->latT = 0.0;
	self->lonL = 0.0;
	self->latB = 0.0;
	self->lonR = 0.0;
}

void osmdb_range_addPt(osmdb_range_t* self,
                       double lat, double lon)
{
	assert(self);

	if(self->pts)
	{
		if(lat > self->latT)
		{
			self->latT = lat;
		}
		else if(lat < self->latB)
		{
			self->latB = lat;
		}

		if(lon < self->lonL)
		{
			self->lonL = lon;
		}
		else if(lon > self->lonR)
		{
			self->lonR = lon;
		}
	}
	else
	{
		self->latT = lat;
		self->lonL = lon;
		self->latB = lat;
		self->lonR = lon;
	}
	++self->pts;
}

void osmdb_range_addNode(osmdb_range_t* self,
                         osmdb_node_t* node)
{
	assert(self);
	assert(node);

	if(self->pts)
	{
		if(node->lat > self->latT)
		{
			self->latT = node->lat;
		}
		else if(node->lat < self->latB)
		{
			self->latB = node->lat;
		}

		if(node->lon < self->lonL)
		{
			self->lonL = node->lon;
		}
		else if(node->lon > self->lonR)
		{
			self->lonR = node->lon;
		}
	}
	else
	{
		self->latT = node->lat;
		self->lonL = node->lon;
		self->latB = node->lat;
		self->lonR = node->lon;
	}
	++self->pts;
}

void osmdb_range_addWay(osmdb_range_t* self,
                        struct osmdb_index_s* index,
                        osmdb_way_t* way)
{
	assert(self);
	assert(index);
	assert(way);

	if((way->lat != 0.0) && (way->lon != 0.0))
	{
		osmdb_range_addPt(self, way->lat, way->lon);
		return;
	}

	osmdb_node_t* node;

	a3d_listitem_t* iter = a3d_list_head(way->nds);
	while(iter)
	{
		double* ref = (double*)
		              a3d_list_peekitem(iter);
		node = (osmdb_node_t*)
		       osmdb_index_find(index,
		                        OSMDB_TYPE_NODE,
		                        *ref);
		if(node)
		{
			osmdb_range_addNode(self, node);
		}

		iter = a3d_list_next(iter);
	}
}

void osmdb_range_addRelation(osmdb_range_t* self,
                             struct osmdb_index_s* index,
                             osmdb_relation_t* relation)
{
	assert(self);
	assert(index);
	assert(relation);

	osmdb_node_t* node;
	osmdb_way_t*  way;

	if((relation->lat != 0.0) && (relation->lon != 0.0))
	{
		osmdb_range_addPt(self, relation->lat, relation->lon);
		return;
	}

	a3d_listitem_t* iter = a3d_list_head(relation->members);
	while(iter)
	{
		osmdb_member_t* m = (osmdb_member_t*)
		                    a3d_list_peekitem(iter);
		if(m->type == OSMDB_TYPE_NODE)
		{
			node = (osmdb_node_t*)
			       osmdb_index_find(index,
			                        m->type,
			                        m->ref);
			if(node)
			{
				osmdb_range_addNode(self, node);
			}
		}
		else if(m->type == OSMDB_TYPE_WAY)
		{
			way = (osmdb_way_t*)
			       osmdb_index_find(index,
			                        m->type,
			                        m->ref);
			if(way)
			{
				osmdb_range_addWay(self, index, way);
			}
		}
		// ignore relation members

		iter = a3d_list_next(iter);
	}
}

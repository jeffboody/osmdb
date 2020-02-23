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
#include "libcc/cc_log.h"
#include "libcc/cc_timestamp.h"
#include "libxmlstream/xml_istream.h"
#include "libxmlstream/xml_ostream.h"
#include "osm_parser.h"

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	double t0 = cc_timestamp();

	if(argc != 2)
	{
		LOGE("%s [prefix]", argv[0]);
		return EXIT_FAILURE;
	}

	char fname_in[256];
	char fname_nodes[256];
	char fname_ways[256];
	char fname_relations[256];
	snprintf(fname_in, 256, "%s.osm", argv[1]);
	snprintf(fname_nodes, 256, "%s-nodes.xml.gz", argv[1]);
	snprintf(fname_ways, 256, "%s-ways.xml.gz", argv[1]);
	snprintf(fname_relations, 256, "%s-relations.xml.gz", argv[1]);

	xml_ostream_t* os_nodes = xml_ostream_newGz(fname_nodes);
	if(os_nodes == NULL)
	{
		return EXIT_FAILURE;
	}

	xml_ostream_t* os_ways = xml_ostream_newGz(fname_ways);
	if(os_ways == NULL)
	{
		goto fail_os_ways;
	}

	xml_ostream_t* os_relations;
	os_relations = xml_ostream_newGz(fname_relations);
	if(os_relations == NULL)
	{
		goto fail_os_relations;
	}

	osm_parser_t* parser;
	parser = osm_parser_new(os_nodes, os_ways, os_relations);
	if(parser == NULL)
	{
		goto fail_parser;
	}

	if(xml_istream_parse((void*) parser,
	                     osm_parser_start,
	                     osm_parser_end,
	                     fname_in) == 0)
	{
		goto fail_parse;
	}

	if((xml_ostream_complete(os_nodes)     == 0) ||
	   (xml_ostream_complete(os_ways)      == 0) ||
	   (xml_ostream_complete(os_relations) == 0))
	{
		goto fail_os;
	}

	osm_parser_delete(&parser);
	xml_ostream_delete(&os_relations);
	xml_ostream_delete(&os_ways);
	xml_ostream_delete(&os_nodes);

	// success
	LOGI("SUCCESS dt=%lf", cc_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_os:
	fail_parse:
		osm_parser_delete(&parser);
	fail_parser:
		xml_ostream_delete(&os_relations);
	fail_os_relations:
		xml_ostream_delete(&os_ways);
	fail_os_ways:
		xml_ostream_delete(&os_nodes);
	LOGI("FAILURE dt=%lf", cc_timestamp() - t0);
	return EXIT_FAILURE;
}

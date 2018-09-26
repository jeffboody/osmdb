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
#include "a3d/a3d_timestamp.h"
#include "libxmlstream/xml_istream.h"
#include "libxmlstream/xml_ostream.h"
#include "osm_parser.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	double t0 = a3d_timestamp();

	if(argc != 3)
	{
		LOGE("%s in.osm out.xmlz", argv[0]);
		return EXIT_FAILURE;
	}

	xml_ostream_t* os = xml_ostream_newGz(argv[2]);
	if(os == NULL)
	{
		return EXIT_FAILURE;
	}

	osm_parser_t* parser = osm_parser_new(os);
	if(parser == NULL)
	{
		goto fail_parser;
	}

	if(xml_istream_parse((void*) parser,
	                     osm_parser_start,
	                     osm_parser_end,
	                     argv[1]) == 0)
	{
		goto fail_parse;
	}

	if(xml_ostream_complete(os) == 0)
	{
		goto fail_os;
	}

	osm_parser_delete(&parser);
	xml_ostream_delete(&os);

	// success
	LOGI("SUCCESS dt=%lf", a3d_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_os:
	fail_parse:
		osm_parser_delete(&parser);
	fail_parser:
		xml_ostream_delete(&os);
	LOGI("FAILURE dt=%lf", a3d_timestamp() - t0);
	return EXIT_FAILURE;
}

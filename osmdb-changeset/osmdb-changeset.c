/*
 * Copyright (c) 2020 Jeff Boody
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
#include "osm_parser.h"

/***********************************************************
* public                                                   *
***********************************************************/

int main(int argc, char** argv)
{
	double t0 = cc_timestamp();

	if(argc != 4)
	{
		LOGE("%s change_id changeset.osm file.sqlite3", argv[0]);
		return EXIT_FAILURE;
	}

	double      change_id = strtod(argv[1], NULL);
	const char* changeset = argv[2];
	const char* fname     = argv[3];

	osm_parser_t* parser;
	parser = osm_parser_new(change_id, fname);
	if(parser == NULL)
	{
		return EXIT_FAILURE;
	}

	if(xml_istream_parse((void*) parser,
	                     osm_parser_start,
	                     osm_parser_end,
	                     changeset) == 0)
	{
		goto fail_parse;
	}

	if(osm_parser_finish(parser) == 0)
	{
		goto fail_finish;
	}

	osm_parser_delete(&parser);

	// success
	LOGI("SUCCESS dt=%lf", cc_timestamp() - t0);
	return EXIT_SUCCESS;

	// failure
	fail_finish:
	fail_parse:
		osm_parser_delete(&parser);
	LOGI("FAILURE dt=%lf", cc_timestamp() - t0);
	return EXIT_FAILURE;
}

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

#ifndef osmdb_parser_H
#define osmdb_parser_H

#include "osmdb_node.h"
#include "osmdb_way.h"
#include "osmdb_relation.h"

typedef int (*osmdb_parser_nodeFn)(void* priv,
                                   osmdb_node_t* node);
typedef int (*osmdb_parser_wayFn)(void* priv,
                                  osmdb_way_t* way);
typedef int (*osmdb_parser_relationFn)(void* priv,
                                       osmdb_relation_t* relation);
typedef int (*osmdb_parser_nodeRefFn)(void* priv,
                                      double ref);
typedef int (*osmdb_parser_wayRefFn)(void* priv,
                                     double ref);
typedef int (*osmdb_parser_relationRefFn)(void* priv,
                                          double ref);

int osmdb_parse(const char* fname, void* priv,
                osmdb_parser_nodeFn node_fn,
                osmdb_parser_wayFn way_fn,
                osmdb_parser_relationFn relation_fn);

int osmdb_parseRefs(const char* fname, void* priv,
                    osmdb_parser_nodeRefFn nref_fn,
                    osmdb_parser_wayRefFn wref_fn,
                    osmdb_parser_relationRefFn rref_fn);

#endif

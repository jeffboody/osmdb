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

#ifndef osmdb_util_H
#define osmdb_util_H

// st conversions
int         osmdb_stNameToCode(const char* name);
int         osmdb_stAbrevToCode(const char* abrev);
const char* osmdb_stCodeToName(int code);
const char* osmdb_stCodeToAbrev(int code);

// class conversions
int         osmdb_classNameToCode(const char* name);
int         osmdb_classKVToCode(const char* k, const char* v);
const char* osmdb_classCodeToName(int code);
int         osmdb_classCodeToRank(int code);
int         osmdb_classIsBuilding(int code);
int         osmdb_classCount(void);

// relation tag type conversions
int         osmdb_relationTagTypeToCode(const char* type);
const char* osmdb_relationTagCodeToType(int code);

// relation member type conversions
int         osmdb_relationMemberTypeToCode(const char* type);
const char* osmdb_relationMemberCodeToType(int code);

// relation member role conversions
int         osmdb_relationMemberRoleToCode(const char* role);
const char* osmdb_relationMemberCodeToRole(int code);

// file operations
int osmdb_fileExists(const char* fname);
int osmdb_mkdir(const char* path);

// index/chunk id
void osmdb_splitId(double id,
                   double* idu, double* idl);

#endif

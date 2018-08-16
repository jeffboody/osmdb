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
#include <stdio.h>
#include "osmdb_util.h"

#define LOG_TAG "osmdb"
#include "libxmlstream/xml_log.h"

/***********************************************************
* private                                                  *
***********************************************************/

typedef struct
{
	char st[3];
	char state[32];
} osmdb_utilST_t;

// https://en.wikipedia.org/wiki/Federal_Information_Processing_Standard_state_code
osmdb_utilST_t OSM_UTIL_ST[60] =
{
	{ .st="",   .state=""                     },   // 0
	{ .st="AL", .state="Alabama"              },   // 1
	{ .st="AK", .state="Alaska"               },   // 2
	{ .st="",   .state=""                     },   // 3
	{ .st="AZ", .state="Arizona"              },   // 4
	{ .st="AR", .state="Arkansas"             },   // 5
	{ .st="CA", .state="California"           },   // 6
	{ .st="",   .state=""                     },   // 7
	{ .st="CO", .state="Colorado"             },   // 8
	{ .st="CT", .state="Connecticut"          },   // 9
	{ .st="DE", .state="Delaware"             },   // 10
	{ .st="DC", .state="District of Columbia" },   // 11
	{ .st="FL", .state="Florida"              },   // 12
	{ .st="GA", .state="Georgia"              },   // 13
	{ .st="",   .state=""                     },   // 14
	{ .st="HI", .state="Hawaii"               },   // 15
	{ .st="ID", .state="Idaho"                },   // 16
	{ .st="IL", .state="Illinois"             },   // 17
	{ .st="IN", .state="Indiana"              },   // 18
	{ .st="IA", .state="Iowa"                 },   // 19
	{ .st="KS", .state="Kansas"               },   // 20
	{ .st="KY", .state="Kentucky"             },   // 21
	{ .st="LA", .state="Louisiana"            },   // 22
	{ .st="ME", .state="Maine"                },   // 23
	{ .st="MD", .state="Maryland"             },   // 24
	{ .st="MA", .state="Massachusetts"        },   // 25
	{ .st="MI", .state="Michigan"             },   // 26
	{ .st="MN", .state="Minnesota"            },   // 27
	{ .st="MS", .state="Mississippi"          },   // 28
	{ .st="MO", .state="Missouri"             },   // 29
	{ .st="MT", .state="Montana"              },   // 30
	{ .st="NE", .state="Nebraska"             },   // 31
	{ .st="NV", .state="Nevada"               },   // 32
	{ .st="NH", .state="New Hampshire"        },   // 33
	{ .st="NJ", .state="New Jersey"           },   // 34
	{ .st="NM", .state="New Mexico"           },   // 35
	{ .st="NY", .state="New York"             },   // 36
	{ .st="NC", .state="North Carolina"       },   // 37
	{ .st="ND", .state="North Dakota"         },   // 38
	{ .st="OH", .state="Ohio"                 },   // 39
	{ .st="OK", .state="Oklahoma"             },   // 40
	{ .st="OR", .state="Oregon"               },   // 41
	{ .st="PA", .state="Pennsylvania"         },   // 42
	{ .st="",   .state=""                     },   // 43
	{ .st="RI", .state="Rhode Island"         },   // 44
	{ .st="SC", .state="South Carolina"       },   // 45
	{ .st="SD", .state="South Dakota"         },   // 46
	{ .st="TN", .state="Tennessee"            },   // 47
	{ .st="TX", .state="Texas"                },   // 48
	{ .st="UT", .state="Utah"                 },   // 49
	{ .st="VT", .state="Vermont"              },   // 50
	{ .st="VA", .state="Virginia"             },   // 51
	{ .st="",   .state=""                     },   // 52
	{ .st="WA", .state="Washington"           },   // 53
	{ .st="WV", .state="West Virginia"        },   // 54
	{ .st="WI", .state="Wisconsin"            },   // 55
	{ .st="WY", .state="Wyoming"              },   // 56
	{ .st="",   .state=""                     },   // 57
	{ .st="",   .state=""                     },   // 58
	{ .st="",   .state=""                     },   // 59
};

// https://wiki.openstreetmap.org/wiki/Map_Features
const char* const OSM_UTIL_CLASSES[] =
{
	"class:none",
	"place:state",
	"place:city",
	"place:town",
	"place:village",
	"place:hamlet",
	"place:isolated_dwelling",
	"place:county",
	"landuse:forest",
	"leisure:park",
	"natural:peak",
	"natural:volcano",
	"natural:saddle",
	"natural:wood",
	"natural:scrub",
	"natural:heath",
	"natural:grassland",
	"natural:fell",
	"natural:bare_rock",
	"natural:scree",
	"natural:shingle",
	"natural:sand",
	"natural:mud",
	"natural:water",
	"natural:wetland",
	"natural:glacier",
	"natural:bay",
	"natural:cape",
	"natural:beach",
	"natural:coastline",
	"natural:hot_spring",
	"natural:geyser",
	"natural:valley",
	"natural:ridge",
	"natural:arete",
	"natural:cliff",
	"natural:rock",
	"natural:stone",
	"natural:sinkhole",
	"natural:cave_entrance",
	"waterway:dam",
	"waterway:waterfall",
	"aeroway:aerodrome",
	"aeroway:helipad",
	"amenity:college",
	"amenity:library",
	"amenity:university",
	"amenity:hospital",
	"amenity:veterinary",
	"amenity:planetarium",
	"amenity:police",
	"historic:aircraft",
	"historic:aqueduct",
	"historic:archaeological_site",
	"historic:battlefield",
	"historic:castle",
	"historic:farm",
	"historic:memorial",
	"historic:monument",
	"historic:railway_car",
	"historic:ruins",
	"historic:ship",
	"historic:tomb",
	"historic:wreck",
	"tourism:attraction",
	"tourism:camp_site",
	"tourism:museum",
	"tourism:zoo",
	NULL
};

const char* const OSM_UTIL_RELATION_TAG_TYPE[] =
{
	"none",
	"boundary",
	"multipolygon",
	NULL
};

const char* const OSM_UTIL_RELATION_MEMBER_TYPE[] =
{
	"none",
	"node",
	"way",
	"relation",
	NULL
};

const char* const OSM_UTIL_RELATION_MEMBER_ROLE[] =
{
	"none",
	"outer",
	"inner",
	"admin_centre",
	"label",
	NULL
};

/***********************************************************
* public                                                   *
***********************************************************/

int osmdb_stNameToCode(const char* name)
{
	assert(name);

	int i;
	for(i = 0; i < 60; ++i)
	{
		if(strcmp(name, OSM_UTIL_ST[i].state) == 0)
		{
			return i;
		}
	}

	return 0;
}

int osmdb_stAbrevToCode(const char* abrev)
{
	assert(abrev);

	if((abrev[0] != '\0') &&
	   (abrev[1] != '\0') &&
	   (abrev[2] == '\0'))
	{
		int i;
		for(i = 0; i < 60; ++i)
		{
			char a = abrev[0];
			char b = abrev[1];

			// to upper case
			if((a >= 'a') && (a <= 'z'))
			{
				a = a - 'a' + 'A';
			}
			if((b >= 'a') && (b <= 'z'))
			{
				b = b - 'a' + 'A';
			}

			// find abrev in table
			if((a == OSM_UTIL_ST[i].st[0]) &&
			   (b == OSM_UTIL_ST[i].st[1]))
			{
				return i;
			}
		}
	}

	return 0;
}

const char* osmdb_stCodeToName(int code)
{
	assert((code >= 0) && (code < 60));

	return OSM_UTIL_ST[code].state;
}

const char* osmdb_stCodeToAbrev(int code)
{
	assert((code >= 0) && (code < 60));

	return OSM_UTIL_ST[code].st;
}

int osmdb_classNameToCode(const char* name)
{
	assert(name);

	int idx = 0;
	while(OSM_UTIL_CLASSES[idx])
	{
		if(strcmp(OSM_UTIL_CLASSES[idx], name) == 0)
		{
			return idx;
		}

		++idx;
	}

	return 0;
}

int osmdb_classKVToCode(const char* k, const char* v)
{
	assert(k);
	assert(v);

	char name[256];
	snprintf(name, 256, "%s:%s", k, v);
	return osmdb_classNameToCode(name);
}

const char* osmdb_classCodeToName(int code)
{
	int idx = 0;
	while(OSM_UTIL_CLASSES[idx])
	{
		if(idx == code)
		{
			return OSM_UTIL_CLASSES[code];
		}
		++idx;
	}
	return OSM_UTIL_CLASSES[0];
}

int osmdb_relationTagTypeToCode(const char* type)
{
	assert(type);

	int idx = 0;
	while(OSM_UTIL_RELATION_TAG_TYPE[idx])
	{
		if(strcmp(OSM_UTIL_RELATION_TAG_TYPE[idx], type) == 0)
		{
			return idx;
		}

		++idx;
	}

	return 0;
}

const char* osmdb_relationTagCodeToType(int code)
{
	int idx = 0;
	while(OSM_UTIL_RELATION_TAG_TYPE[idx])
	{
		if(idx == code)
		{
			return OSM_UTIL_RELATION_TAG_TYPE[code];
		}
		++idx;
	}
	return OSM_UTIL_RELATION_TAG_TYPE[0];
}

int osmdb_relationMemberTypeToCode(const char* type)
{
	assert(type);

	int idx = 0;
	while(OSM_UTIL_RELATION_MEMBER_TYPE[idx])
	{
		if(strcmp(OSM_UTIL_RELATION_MEMBER_TYPE[idx], type) == 0)
		{
			return idx;
		}

		++idx;
	}

	return 0;
}

const char* osmdb_relationMemberCodeToType(int code)
{
	int idx = 0;
	while(OSM_UTIL_RELATION_MEMBER_TYPE[idx])
	{
		if(idx == code)
		{
			return OSM_UTIL_RELATION_MEMBER_TYPE[code];
		}
		++idx;
	}
	return OSM_UTIL_RELATION_MEMBER_TYPE[0];
}

int osmdb_relationMemberRoleToCode(const char* role)
{
	assert(role);

	int idx = 0;
	while(OSM_UTIL_RELATION_MEMBER_ROLE[idx])
	{
		if(strcmp(OSM_UTIL_RELATION_MEMBER_ROLE[idx], role) == 0)
		{
			return idx;
		}

		++idx;
	}

	return 0;
}

const char* osmdb_relationMemberCodeToRole(int code)
{
	int idx = 0;
	while(OSM_UTIL_RELATION_MEMBER_ROLE[idx])
	{
		if(idx == code)
		{
			return OSM_UTIL_RELATION_MEMBER_ROLE[code];
		}
		++idx;
	}
	return OSM_UTIL_RELATION_MEMBER_ROLE[0];
}

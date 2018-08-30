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
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "osmdb_chunk.h"
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
	"aerialway:cable_car",
	"aerialway:gondola",
	"aerialway:chair_lift",
	"aerialway:mixed_lift",
	"aerialway:drag_lift",
	"aerialway:t-bar",
	"aerialway:j-bar",
	"aerialway:platter",
	"aerialway:rope_tow",
	"aerialway:magic_carpet",
	"aerialway:zip_line",
	"aerialway:pylon",
	"aerialway:station",
	"aerialway:canopy",   // obsolete - convert to zipline
	"aeroway:aerodrome",
	"aeroway:apron",
	"aeroway:gate",
	"aeroway:hangar",
	"aeroway:helipad",
	"aeroway:heliport",
	"aeroway:navigationalaid",
	"aeroway:runway",
	"aeroway:spaceport",
	"aeroway:taxilane",
	"aeroway:taxiway",
	"aeroway:terminal",
	"aeroway:windsock",
	"amenity:bar",
	"amenity:bbq",
	"amenity:biergarten",
	"amenity:cafe",
	"amenity:drinking_water",
	"amenity:fast_food",
	"amenity:food_court",
	"amenity:ice_cream",
	"amenity:pub",
	"amenity:restaurant",
	"amenity:college",
	"amenity:kindergarten",
	"amenity:library",
	"amenity:archive",
	"amenity:public_bookcase",
	"amenity:school",
	"amenity:music_school",
	"amenity:driving_school",
	"amenity:language_school",
	"amenity:university",
	"amenity:research_institute",
	"amenity:bicycle_parking",
	"amenity:bicycle_repair_station",
	"amenity:bicycle_rental",
	"amenity:boat_rental",
	"amenity:boat_sharing",
	"amenity:buggy_parking",
	"amenity:bus_station",
	"amenity:car_rental",
	"amenity:car_sharing",
	"amenity:car_wash",
	"amenity:charging_station",
	"amenity:ferry_terminal",
	"amenity:fuel",
	"amenity:grit_bin",
	"amenity:motorcycle_parking",
	"amenity:parking",
	"amenity:parking_entrance",
	"amenity:parking_space",
	"amenity:taxi",
	"amenity:ticket_validator",
	"amenity:atm",
	"amenity:bank",
	"amenity:bureau_de_change",
	"amenity:baby_hatch",
	"amenity:clinic",
	"amenity:dentist",
	"amenity:doctors",
	"amenity:hospital",
	"amenity:nursing_home",
	"amenity:pharmacy",
	"amenity:social_facility",
	"amenity:veterinary",
	"healthcare:blood_donation",
	"amenity:arts_centre",
	"amenity:brothel",
	"amenity:casino",
	"amenity:cinema",
	"amenity:community_centre",
	"amenity:fountain",
	"amenity:gambling",
	"amenity:nightclub",
	"amenity:planetarium",
	"amenity:social_centre",
	"amenity:stripclub",
	"amenity:studio",
	"amenity:swingerclub",
	"amenity:theatre",
	"amenity:animal_boarding",
	"amenity:animal_shelter",
	"amenity:baking_oven",
	"amenity:bench",
	"amenity:clock",
	"amenity:courthouse",
	"amenity:coworking_space",
	"amenity:crematorium",
	"amenity:crypt",
	"amenity:dive_centre",
	"amenity:dojo",
	"amenity:embassy",
	"amenity:fire_station",
	"amenity:game_feeding",
	"amenity:grave_yard",
	"amenity:hunting_stand",
	"amenity:internet_cafe",
	"amenity:kitchen",
	"amenity:kneipp_water_cure",
	"amenity:marketplace",
	"amenity:photo_booth",
	"amenity:place_of_warship",
	"amenity:police",
	"amenity:post_box",
	"amenity:post_office",
	"amenity:prison",
	"amenity:public_bath",
	"amenity:ranger_station",
	"amenity:recycling",
	"amenity:rescue_station",
	"amenity:sanitary_dump_station",
	"amenity:shelter",
	"amenity:shower",
	"amenity:table",
	"amenity:telephone",
	"amenity:toilets",
	"amenity:townhall",
	"amenity:vending_machine",
	"amenity:waste_basket",
	"amenity:waste_disposal",
	"amenity:waste_transfer_station",
	"amenity:watering_place",
	"amenity:water_point",
	"barrier:cable_barrier",
	"barrier:city_wall",
	"barrier:ditch",
	"barrier:fence",
	"barrier:guard_rail",
	"barrier:handrail",
	"barrier:hedge",
	"barrier:kerb",
	"barrier:retaining_wall",
	"barrier:tank_trap",
	"barrier:wall",
	"barrier:block",
	"barrier:bollard",
	"barrier:border_control",
	"barrier:bump_gate",
	"barrier:bus_trap",
	"barrier:cattle_grid",
	"barrier:chain",
	"barrier:cycle_barrier",
	"barrier:debris",
	"barrier:entrance",
	"barrier:full-height_turnstyle",
	"barrier:gate",
	"barrier:hampshire_gate",
	"barrier:height_restrictor",
	"barrier:horse_stile",
	"barrier:jersey_barrier",
	"barrier:kent_carriage_gap",
	"barrier:kissing_gate",
	"barrier:lift_gate",
	"barrier:log",
	"barrier:motorcycle_barrier",
	"barrier:rope",
	"barrier:sally_port",
	"barrier:spikes",
	"barrier:stile",
	"barrier:sump_buster",
	"barrier:swing_gate",
	"barrier:toll_booth",
	"barrier:turnstile",
	"barrier:yes",
	"boundary:administrative",
	"boundary:historic",
	"boundary:maritime",
	"boundary:national_park",
	"boundary:political",
	"boundary:postal_code",
	"boundary:religious_administration",
	"boundary:protected_area",
	"building:apartments",
	"building:farm",
	"building:hotel",
	"building:house",
	"building:detached",
	"building:residential",
	"building:dormatory",
	"building:terrace",
	"building:houseboat",
	"building:bungalow",
	"building:static_caravan",
	"building:cabin",
	"building:commercial",
	"building:office",
	"building:industrial",
	"building:retail",
	"building:warehouse",
	"building:kiosk",
	"building:religious",
	"building:cathedral",
	"building:chapel",
	"building:church",
	"building:mosque",
	"building:temple",
	"building:synagogue",
	"building:shrine",
	"building:bakehouse",
	"building:kindergarten",
	"building:civic",
	"building:hospital",
	"building:school",
	"building:stadium",
	"building:train_station",
	"building:transportation",
	"building:university",
	"building:grandstand",
	"building:public",
	"building:barn",
	"building:bridge",
	"building:bunker",
	"building:carport",
	"building:conservatory",
	"building:construction",
	"building:cowshed",
	"building:digester",
	"building:farm_auxilary",
	"building:garage",
	"building:garages",
	"building:garbage_shed",
	"building:greenhouse",
	"building:hangar",
	"building:hut",
	"building:pavilion",
	"building:parking",
	"building:riding_hall",
	"building:roof",
	"building:shed",
	"building:sports_hall",
	"building:stable",
	"building:sty",
	"building:transformer_tower",
	"building:service",
	"building:ruins",
	"building:water_tower",
	"building:yes",
	"craft:agricultural_engines",
	"craft:bakery",
	"craft:basket_maker",
	"craft:beekeeper",
	"craft:blacksmith",
	"craft:boatbuilder",
	"craft:bookbinder",
	"craft:brewery",
	"craft:builder",
	"craft:carpenter",
	"craft:carpet_layer",
	"craft:caterer",
	"craft:chimney_sweeper",
	"craft:clockmaker",
	"craft:confectionery",
	"craft:dental_technican",
	"craft:distillery",
	"craft:dressmaker",
	"craft:embroiderer",
	"craft:electrician",
	"craft:engraver",
	"craft:floorer",
	"craft:gardener",
	"craft:glaziery",
	"craft:handicraft",
	"craft:hvac",
	"craft:insulation",
	"craft:jeweller",
	"craft:joiner",
	"craft:key_cutter",
	"craft:locksmith",
	"craft:metal_construction",
	"craft:mint",
	"craft:optician",
	"craft:painter",
	"craft:parquet_layer,"
	"craft:photographer",
	"craft:photographic_laboratory",
	"craft:piano_tuner",
	"craft:plasterer",
	"craft:plumber",
	"craft:pottery",
	"craft:printmaker",
	"craft:rigger",
	"craft:roofer",
	"craft:saddler",
	"craft:sailmaker",
	"craft:sawmill",
	"craft:scaffolder",
	"craft:sculpter",
	"craft:shoemaker",
	"craft:stand_builder",
	"craft:stonemason",
	"craft:sun_protection",
	"craft:tailor",
	"craft:tiler",
	"craft:tinsmith",
	"craft:toolmaker",
	"craft:turner",
	"craft:upholsterer",
	"craft:watchmaker",
	"craft:window_construction",
	"craft:winery",
	"emergency:ambulance_station",
	"emergency:defibrillator",
	"emergency:first_aid_kit",
	"emergency:landing_site",
	"emergency:emergency_ward_entrance",
	"emergency:dry_riser_inlet",
	"emergency:fire_alarm_box",
	"emergency:fire_extinguisher",
	"emergency:fire_flapper",
	"emergency:fire_hose",
	"emergency:fire_hydrant",
	"emergency:water_tank",
	"emergency:fire_water_pond",
	"emergency:suction_point",
	"emergency:lifeguard",
	"emergency:lifeguard_base",
	"emergency:lifeguard_tower",
	"emergency:lifeguard_platform",
	"emergency:life_ring",
	"emergency:mountain_rescue",
	"emergency:ses_station",
	"emergency:assembly_point",
	"emergency:access_point",
	"emergency:phone",
	"emergency:rescue_box",
	"emergency:siren",
	"geological:moraine",
	"geological:outcrop",
	"geological:palaeontological_site",
	"highway:motorway",
	"highway:trunk",
	"highway:primary",
	"highway:secondary",
	"highway:tertiary",
	"highway:unclassified",
	"highway:residential",
	"highway:service",
	"highway:motorway_link",
	"highway:trunk_link",
	"highway:primary_link",
	"highway:secondary_link",
	"highway:tertiary_link",
	"highway:living_street",
	"highway:pedestrian",
	"highway:track",
	"highway:bus_guideway",
	"highway:escape",
	"highway:raceway",
	"highway:road",
	"highway:footway",
	"highway:bridleway",
	"highway:steps",
	"highway:path",
	"highway:cycleway",
	"highway:bus_stop",
	"highway:crossing",
	"highway:elevator",
	"highway:emergency_access_point",
	"highway:give_way",
	"highway:mini_roundabout",
	"highway:motorway_junction",
	"highway:passing_place",
	"highway:rest_area",
	"highway:speed_camera",
	"highway:street_lamp",
	"highway:services",
	"highway:stop",
	"highway:traffic_signals",
	"highway:turning_circle",
	"historic:aircraft",
	"historic:aqueduct",
	"historic:archaeological_site",
	"historic:battlefield",
	"historic:boundary_stone",
	"historic:building",
	"historic:cannon",
	"historic:castle",
	"historic:castle_wall",
	"historic:church",
	"historic:city_gate",
	"historic:citywalls",
	"historic:farm",
	"historic:fort",
	"historic:gallows",
	"historic:highwater_mark",
	"historic:locomotive",
	"historic:manor",
	"historic:memorial",
	"historic:milestone",
	"historic:monastery",
	"historic:monument",
	"historic:optical_telegraph",
	"historic:pillory",
	"historic:railway_car",
	"historic:ruins",
	"historic:rune_stone",
	"historic:ship",
	"historic:tomb",
	"historic:tower",
	"historic:wayside_cross",
	"historic:wayside_shrine",
	"historic:wreck",
	"historic:yes",
	"landuse:commercial",
	"landuse:construction",
	"landuse:industrial",
	"landuse:residential",
	"landuse:retail",
	"landuse:allotments",
	"landuse:basin",
	"landuse:brownfield",
	"landuse:cemetery",
	"landuse:depot",
	"landuse:farmland",
	"landuse:farmyard",
	"landuse:forest",
	"landuse:garages",
	"landuse:grass",
	"landuse:greenfield",
	"landuse:greenhouse_horticulture",
	"landuse:landfill",
	"landuse:meadow",
	"landuse:military",
	"landuse:orchard",
	"landuse:plant_nursery",
	"landuse:port",
	"landuse:quarry",
	"landuse:railway",
	"landuse:recreation_ground",
	"landuse:religious",
	"landuse:reservoir",
	"landuse:salt_pond",
	"landuse:village_green",
	"landuse:vineyard",
	"leisure:adult_gaming_centre",
	"leisure:amusement_arcade",
	"leisure:beach_resort",
	"leisure:bandstand",
	"leisure:bird_hide",
	"leisure:common",
	"leisure:dance",
	"leisure:disc_golf_course",
	"leisure:dog_park",
	"leisure:escape_game",
	"leisure:firepit",
	"leisure:fishing",
	"leisure:fitness_centre",
	"leisure:fitness_station",
	"leisure:garden",
	"leisure:hackerspace",
	"leisure:horse_riding",
	"leisure:ice_rink",
	"leisure:marina",
	"leisure:minature_golf",
	"leisure:nature_reserve",
	"leisure:park",
	"leisure:picnic_table",
	"leisure:pitch",
	"leisure:playground",
	"leisure:slipway",
	"leisure:sports_centre",
	"leisure:stadium",
	"leisure:summer_camp",
	"leisure:swimming_area",
	"leisure:swimming_pool",
	"leisure:track",
	"leisure:water_park",
	"leisure:wildlife_hide",
	"man_made:adit",
	"man_made:beacon",
	"man_made:breakwater",
	"man_made:bridge",
	"man_made:bunker_silo",
	"man_made:campanile",
	"man_made:chimney",
	"man_made:communications_tower",
	"man_made:crane",
	"man_made:cross",
	"man_made:cutline",
	"man_made:clearcut",
	"man_made:dovecote",
	"man_made:drinking_fountain",
	"man_made:dyke",
	"man_made:embankment",
	"man_made:flagpole",
	"man_made:gasometer",
	"man_made:groyne",
	"man_made:guy",
	"man_made:kiln",
	"man_made:lighthouse",
	"man_made:mast",
	"man_made:mineshaft",
	"man_made:monitoring_station",
	"man_made:obelisk",
	"man_made:observatory",
	"man_made:offshore_platform",
	"man_made:petroleum_well",
	"man_made:pier",
	"man_made:pipeline",
	"man_made:pumping_station",
	"man_made:reservoir_covered",
	"man_made:silo",
	"man_made:snow_fence",
	"man_made:snow_net",
	"man_made:storage_tank",
	"man_made:street_cabinet",
	"man_made:surveillance",
	"man_made:survey_point",
	"man_made:telescope",
	"man_made:tower",
	"man_made:wastewater_plant",
	"man_made:watermill",
	"man_made:water_tower",
	"man_made:water_well",
	"man_made:water_tap",
	"man_made:water_works",
	"man_made:wildlife_crossing",
	"man_made:windmill",
	"man_made:works",
	"man_made:yes",
	"military:airfield",
	"military:ammunition",
	"military:bunker",
	"military:barracks",
	"military:checkpoint",
	"military:danger_area",
	"military:naval_base",
	"military:nuclear_explosion_site",
	"military:obstacle_course",
	"military:office",
	"military:range",
	"military:training_area",
	"military:trench",
	"military:launchpad",
	"natural:wood",
	"natural:tree_row",
	"natural:tree",
	"natural:scrub",
	"natural:heath",
	"natural:moor",
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
	"natural:spring",
	"natural:hot_spring",
	"natural:geyser",
	"natural:blowhole",
	"natural:peak",
	"natural:volcano",
	"natural:valley",
	"natural:ridge",
	"natural:arete",
	"natural:cliff",
	"natural:saddle",
	"natural:rock",
	"natural:stone",
	"natural:sinkhole",
	"natural:cave_entrance",
	"office:accountant",
	"office:adoption_agency",
	"office:advertising_agency",
	"office:architect",
	"office:association",
	"office:charity",
	"office:company",
	"office:educational_institution",
	"office:employment_agency",
	"office:energy_supplier",
	"office:estate_agent",
	"office:forestry",
	"office:foundation",
	"office:government",
	"office:guide",
	"office:healer",
	"office:insurance",
	"office:it",
	"office:lawyer",
	"office:logistics",
	"office:moving_company",
	"office:newspaper",
	"office:ngo",
	"office:notary",
	"office:physican",
	"office:political_party",
	"office:private_investigator",
	"office:property_management",
	"office:quango",
	"office:real_estate_agent",
	"office:religion",
	"office:research",
	"office:surveyor",
	"office:tax",
	"office:tax_advisor",
	"office:telecommunication",
	"office:therapist",
	"office:travel_agent",
	"office:water_utility",
	"office:yes",
	"place:country",
	"place:state",
	"place:region",
	"place:province",
	"place:district",
	"place:county",
	"place:municipality",
	"place:city",
	"place:borough",
	"place:suburb",
	"place:quarter",
	"place:neighbourhood",
	"place:city_block",
	"place:plot",
	"place:town",
	"place:village",
	"place:hamlet",
	"place:isolated_dwelling",
	"place:farm",
	"place:allotments",
	"place:continent",
	"place:archipelago",
	"place:island",
	"place:islet",
	"place:square",
	"place:locality",
	"power:plant",
	"power:cable",
	"power:compensator",
	"power:convertor",
	"power:generator",
	"power:heliostat",
	"power:insulator",
	"power:line",
	"line:busbar",
	"line:bay",
	"power:minor_line",
	"power:pole",
	"power:portal",
	"power:catenary_mast",
	"power:substation",
	"power:switch",
	"power:terminal",
	"power:tower",
	"power:transformer",
	"public_transport:stop_position",
	"public_transport:platform",
	"public_transport:station",
	"public_transport:stop_area",
	"railway:abandoned",
	"railway:construction",
	"railway:disused",
	"railway:funicular",
	"railway:light_rail",
	"railway:minature",
	"railway:monorail",
	"railway:narrow_gauge",
	"railway:preserved",
	"railway:rail",
	"railway:subway",
	"railway:tram",
	"railway:halt",
	"railway:platform",
	"railway:station",
	"railway:subway_entrance",
	"railway:tram_stop",
	"railway:buffer_stop",
	"railway:derail",
	"railway:crossing",
	"railway:level_crossing",
	"railway:signal",
	"railway:switch",
	"railway:railway_crossing",
	"railway:turntable",
	"railway:roundhouse",
	"railway:traverser",
	"railway:wash",
	"shop:alcohol",
	"shop:bakery",
	"shop:beverages",
	"shop:brewing_supplies",
	"shop:butcher",
	"shop:cheese",
	"shop:chocolate",
	"shop:coffee",
	"shop:confectionery",
	"shop:convenience",
	"shop:deli",
	"shop:dairy",
	"shop:farm",
	"shop:frozen_food",
	"shop:greengrocier",
	"shop:health_food",
	"shop:ice_cream",
	"shop:pasta",
	"shop:pastry",
	"shop:seafood",
	"shop:spices",
	"shop:tea",
	"shop:water",
	"shop:department_store",
	"shop:general",
	"shop:kiosk",
	"shop:mall",
	"shop:supermarket",
	"shop:wholesale",
	"shop:baby_goods",
	"shop:bag",
	"shop:boutique",
	"shop:clothes",
	"shop:fabric",
	"shop:fashion",
	"shop:jewelry",
	"shop:leather",
	"shop:sewing",
	"shop:shoes",
	"shop:tailor",
	"shop:watches",
	"shop:charity",
	"shop:second_hand",
	"shop:variety_store",
	"shop:beauty",
	"shop:chemist",
	"shop:cosmetics",
	"shop:erotic",
	"shop:hairdresser",
	"shop:hairdresser_suply",
	"shop:hearing_aids",
	"shop:herbalist",
	"shop:massage",
	"shop:medical_supply",
	"shop:nutrition_supplements",
	"shop:optician",
	"shop:perfumery",
	"shop:tattoo",
	"shop:agrarian",
	"shop:appliance",
	"shop:bathroom_furnishing",
	"shop:doityourself",
	"shop:electrical",
	"shop:energy",
	"shop:fireplace",
	"shop:florist",
	"shop:garden_centre",
	"shop:garden_furniture",
	"shop:gas",
	"shop:glaziery",
	"shop:hardware",
	"shop:houseware",
	"shop:locksmith",
	"shop:paint",
	"shop:security",
	"shop:trade",
	"shop:antiques",
	"shop:bed",
	"shop:candles",
	"shop:carpet",
	"shop:curtain",
	"shop:doors",
	"shop:flooring",
	"shop:furniture",
	"shop:interior_decoration",
	"shop:kitchen",
	"shop:lamps",
	"shop:tiles",
	"shop:window_blind",
	"shop:computer",
	"shop:robot",
	"shop:electonics",
	"shop:hifi",
	"shop:mobile_phone",
	"shop:radiotechnics",
	"shop:vacuum_cleaner",
	"shop:atv",
	"shop:bicycle",
	"shop:boat",
	"shop:car",
	"shop:car_repair",
	"shop:car_parts",
	"shop:fuel",
	"shop:fishing",
	"shop:free_flying",
	"shop:hunting",
	"shop:jetski",
	"shop:motorcycle",
	"shop:outdoor",
	"shop:scuba_diving",
	"shop:ski",
	"shop:snowmobile",
	"shop:sports",
	"shop:swimming_pool",
	"shop:tyres",
	"shop:art",
	"shop:collector",
	"shop:craft",
	"shop:frame",
	"shop:games",
	"shop:model",
	"shop:music",
	"shop:musical_instrument",
	"shop:photo",
	"shop:camera",
	"shop:trophy",
	"shop:video",
	"shop:video_games",
	"shop:anime",
	"shop:books",
	"shop:gift",
	"shop:lottery",
	"shop:newsagent",
	"shop:stationery",
	"shop:ticket",
	"shop:bookmaker",
	"shop:copyshop",
	"shop:dry_cleaning",
	"shop:e-cigarette",
	"shop:funeral_directors",
	"shop:laundry",
	"shop:money_lender",
	"shop:party",
	"shop:pawnbroker",
	"shop:pet",
	"shop:pyrotechnics",
	"shop:religion",
	"shop:storage_rental",
	"shop:tobacco",
	"shop:toys",
	"shop:travel_agency",
	"shop:vacant",
	"shop:weapons",
	"sport:9pin",
	"sport:10pin",
	"sport:american_football",
	"sport:aikido",
	"sport:archery",
	"sport:athletics",
	"sport:australian_football",
	"sport:badminton",
	"sport:bandy",
	"sport:base",
	"sport:baseball",
	"sport:basketball",
	"sport:beachvolleyball",
	"sport:billards",
	"sport:bmx",
	"sport:bobsleigh",
	"sport:boules",
	"sport:bowls",
	"sport:boxing",
	"sport:canadian_football",
	"sport:canoe",
	"sport:chess",
	"sport:cliff_diving",
	"sport:climbing",
	"sport:climbing_adventure",
	"sport:cricket",
	"sport:croquet",
	"sport:curling",
	"sport:cycling",
	"sport:darts",
	"sport:dog_racing",
	"sport:equestrian",
	"sport:fencing",
	"sport:field_hockey",
	"sport:free_flying",
	"sport:futsal",
	"sport:gaelic_games",
	"sport:golf",
	"sport:gymnastics",
	"sport:handball",
	"sport:hapkido",
	"sport:horseshoes",
	"sport:horse_racing",
	"sport:ice_hockey",
	"sport:ice_skating",
	"sport:ice_stock",
	"sport:judo",
	"sport:karate",
	"sport:karting",
	"sport:kitesurfing",
	"sport:korfball",
	"sport:lacrosse",
	"sport:model_aerodrome",
	"sport:motocross",
	"sport:motor",
	"sport:multi",
	"sport:netball",
	"sport:obstacle_course",
	"sport:orienteering",
	"sport:paddle_tennis",
	"sport:padel",
	"sport:parachuting",
	"sport:paragliding",
	"sport:pelota",
	"sport:racquet",
	"sport:rc_car",
	"sport:roller_skating",
	"sport:rowing",
	"sport:rugby_league",
	"sport:rugby_union",
	"sport:running",
	"sport:sailing",
	"sport:scuba_diving",
	"sport:shooting",
	"sport:skateboard",
	"sport:soccer",
	"sport:sumo",
	"sport:surfing",
	"sport:swimming",
	"sport:table_tennis",
	"sport:table_soccer",
	"sport:taekwondo",
	"sport:tennis",
	"sport:toboggan",
	"sport:volleyball",
	"sport:water_polo",
	"sport:water_ski",
	"sport:weightlifting",
	"sport:wrestling",
	"sport:yoga",
	"tourism:alpine_hut",
	"tourism:apartment",
	"tourism:aquarium",
	"tourism:artwork",
	"tourism:attraction",
	"tourism:camp_site",
	"tourism:caravan_site",
	"tourism:chalet",
	"tourism:gallery",
	"tourism:guest_house",
	"tourism:hostel",
	"tourism:hotel",
	"tourism:information",
	"tourism:motel",
	"tourism:museum",
	"tourism:picnic_site",
	"tourism:theme_park",
	"tourism:viewpoint",
	"tourism:wilderness_hut",
	"tourism:zoo",
	"tourism:yes",
	"waterway:river",
	"waterway:riverbank",
	"waterway:stream",
	"waterway:wadi",
	"waterway:drystream",
	"waterway:canal",
	"waterway:drain",
	"waterway:ditch",
	"waterway:fairway",
	"waterway:dock",
	"waterway:boatyard",
	"waterway:dam",
	"waterway:weir",
	"waterway:stream_end",
	"waterway:waterfall",
	"waterway:lock_gate",
	"waterway:turning_point",
	"waterway:water_point",
	"waterway:fuel",
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

int osmdb_classCount(void)
{
	int idx = 0;
	while(OSM_UTIL_CLASSES[idx])
	{
		++idx;
	}
	return idx;
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

int osmdb_fileExists(const char* fname)
{
	assert(fname);

	if(access(fname, F_OK) != 0)
	{
		return 0;
	}
	return 1;
}

int osmdb_mkdir(const char* path)
{
	assert(path);

	int  len = strnlen(path, 255);
	char dir[256];
	int  i;
	for(i = 0; i < len; ++i)
	{
		dir[i]     = path[i];
		dir[i + 1] = '\0';

		if(dir[i] == '/')
		{
			if(access(dir, R_OK) == 0)
			{
				// dir already exists
				continue;
			}

			// try to mkdir
			if(mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1)
			{
				if(errno == EEXIST)
				{
					// already exists
				}
				else
				{
					LOGE("mkdir %s failed", dir);
					return 0;
				}
			}
		}
	}

	return 1;
}

void osmdb_splitId(double id,
                   double* idu, double* idl)
{
	assert(idu);
	assert(idl);

	// splits id to upper and lower digets
	double s = (double) OSMDB_CHUNK_COUNT;
	id = id/s;
	*idl = s*modf(id, idu);
}

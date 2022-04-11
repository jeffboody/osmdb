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

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "osmdb"
#include "../libcc/cc_log.h"
#include "osmdb_util.h"

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

typedef struct
{
	struct
	{
		unsigned int building : 1;
		unsigned int boundary : 1;
		unsigned int core     : 1;
		unsigned int pad      : 29;
	};
	const char* class;
} osmdb_utilClass_t;

// https://wiki.openstreetmap.org/wiki/Map_Features
// ranks: 0-5 (low to high)
// 5: country/state
// 4: city/town
// 3: peak/volcano/national park/reservoir/lake/glacier/dam/forest
// 2: parks/natural/tourism/airport/university/library/bridge/places
// 1: trails/river
// 0: other
//
// Custom Classes
// * boundary:state is a custom class for state boundaries
//   which are imported from cb_2018_us_state_500k.kml
// * boundary:national_park2 is a custom class for
//   national park boundaries with have
//   boundary:national_park, protect_class=2 and
//   ownership:national
// * boundary:national_monument3 is a custom class for
//   national monument boundaries with have
//   boundary:national_park, protect_class=3 and
//   ownership:national
osmdb_utilClass_t OSM_UTIL_CLASSES[] =
{
	{ .class="class:none",                                     },
	{ .class="aerialway:cable_car",                            },
	{ .class="aerialway:gondola",                              },
	{ .class="aerialway:chair_lift",                           },
	{ .class="aerialway:mixed_lift",                           },
	{ .class="aerialway:drag_lift",                            },
	{ .class="aerialway:t-bar",                                },
	{ .class="aerialway:j-bar",                                },
	{ .class="aerialway:platter",                              },
	{ .class="aerialway:rope_tow",                             },
	{ .class="aerialway:magic_carpet",                         },
	{ .class="aerialway:zip_line",                             },
	{ .class="aerialway:pylon",                                },
	{ .class="aerialway:station",                              },
	{ .class="aerialway:canopy",                               },   // obsolete - convert to zipline
	{ .class="aeroway:aerodrome",                              },
	{ .class="aeroway:apron",                                  },
	{ .class="aeroway:gate",                                   },
	{ .class="aeroway:hangar",                                 },
	{ .class="aeroway:helipad",                                },
	{ .class="aeroway:heliport",                               },
	{ .class="aeroway:navigationalaid",                        },
	{ .class="aeroway:runway",                                 },
	{ .class="aeroway:spaceport",                              },
	{ .class="aeroway:taxilane",                               },
	{ .class="aeroway:taxiway",                                },
	{ .class="aeroway:terminal",                               },
	{ .class="aeroway:windsock",                               },
	{ .class="amenity:bar",                                    },
	{ .class="amenity:bbq",                                    },
	{ .class="amenity:biergarten",                             },
	{ .class="amenity:cafe",                                   },
	{ .class="amenity:drinking_water",                         },
	{ .class="amenity:fast_food",                              },
	{ .class="amenity:food_court",                             },
	{ .class="amenity:ice_cream",                              },
	{ .class="amenity:pub",                                    },
	{ .class="amenity:restaurant",                             },
	{ .class="amenity:college"                                 },
	{ .class="amenity:kindergarten",                           },
	{ .class="amenity:library",                                },
	{ .class="amenity:archive",                                },
	{ .class="amenity:public_bookcase",                        },
	{ .class="amenity:school",                                 },
	{ .class="amenity:music_school",                           },
	{ .class="amenity:driving_school",                         },
	{ .class="amenity:language_school",                        },
	{ .class="amenity:university",                             },
	{ .class="amenity:research_institute",                     },
	{ .class="amenity:bicycle_parking",                        },
	{ .class="amenity:bicycle_repair_station",                 },
	{ .class="amenity:bicycle_rental",                         },
	{ .class="amenity:boat_rental",                            },
	{ .class="amenity:boat_sharing",                           },
	{ .class="amenity:buggy_parking",                          },
	{ .class="amenity:bus_station",                            },
	{ .class="amenity:car_rental",                             },
	{ .class="amenity:car_sharing",                            },
	{ .class="amenity:car_wash",                               },
	{ .class="amenity:charging_station",                       },
	{ .class="amenity:ferry_terminal",                         },
	{ .class="amenity:fuel",                                   },
	{ .class="amenity:grit_bin",                               },
	{ .class="amenity:motorcycle_parking",                     },
	{ .class="amenity:parking",                                },
	{ .class="amenity:parking_entrance",                       },
	{ .class="amenity:parking_space",                          },
	{ .class="amenity:taxi",                                   },
	{ .class="amenity:ticket_validator",                       },
	{ .class="amenity:atm",                                    },
	{ .class="amenity:bank",                                   },
	{ .class="amenity:bureau_de_change",                       },
	{ .class="amenity:baby_hatch",                             },
	{ .class="amenity:clinic",                                 },
	{ .class="amenity:dentist",                                },
	{ .class="amenity:doctors",                                },
	{ .class="amenity:hospital",                               },
	{ .class="amenity:nursing_home",                           },
	{ .class="amenity:pharmacy",                               },
	{ .class="amenity:social_facility",                        },
	{ .class="amenity:veterinary",                             },
	{ .class="healthcare:blood_donation",                      },
	{ .class="amenity:arts_centre",                            },
	{ .class="amenity:brothel",                                },
	{ .class="amenity:casino",                                 },
	{ .class="amenity:cinema",                                 },
	{ .class="amenity:community_centre",                       },
	{ .class="amenity:fountain",                               },
	{ .class="amenity:gambling",                               },
	{ .class="amenity:nightclub",                              },
	{ .class="amenity:planetarium",                            },
	{ .class="amenity:social_centre",                          },
	{ .class="amenity:stripclub",                              },
	{ .class="amenity:studio",                                 },
	{ .class="amenity:swingerclub",                            },
	{ .class="amenity:theatre",                                },
	{ .class="amenity:animal_boarding",                        },
	{ .class="amenity:animal_shelter",                         },
	{ .class="amenity:baking_oven",                            },
	{ .class="amenity:bench",                                  },
	{ .class="amenity:clock",                                  },
	{ .class="amenity:courthouse",                             },
	{ .class="amenity:coworking_space",                        },
	{ .class="amenity:crematorium",                            },
	{ .class="amenity:crypt",                                  },
	{ .class="amenity:dive_centre",                            },
	{ .class="amenity:dojo",                                   },
	{ .class="amenity:embassy",                                },
	{ .class="amenity:fire_station",                           },
	{ .class="amenity:game_feeding",                           },
	{ .class="amenity:grave_yard",                             },
	{ .class="amenity:hunting_stand",                          },
	{ .class="amenity:internet_cafe",                          },
	{ .class="amenity:kitchen",                                },
	{ .class="amenity:kneipp_water_cure",                      },
	{ .class="amenity:marketplace",                            },
	{ .class="amenity:photo_booth",                            },
	{ .class="amenity:place_of_warship",                       },
	{ .class="amenity:police",                                 },
	{ .class="amenity:post_box",                               },
	{ .class="amenity:post_office",                            },
	{ .class="amenity:prison",                                 },
	{ .class="amenity:public_bath",                            },
	{ .class="amenity:ranger_station",                         },
	{ .class="amenity:recycling",                              },
	{ .class="amenity:rescue_station",                         },
	{ .class="amenity:sanitary_dump_station",                  },
	{ .class="amenity:shelter",                                },
	{ .class="amenity:shower",                                 },
	{ .class="amenity:table",                                  },
	{ .class="amenity:telephone",                              },
	{ .class="amenity:toilets",                                },
	{ .class="amenity:townhall",                               },
	{ .class="amenity:vending_machine",                        },
	{ .class="amenity:waste_basket",                           },
	{ .class="amenity:waste_disposal",                         },
	{ .class="amenity:waste_transfer_station",                 },
	{ .class="amenity:watering_place",                         },
	{ .class="amenity:water_point",                            },
	{ .class="barrier:cable_barrier",                          },
	{ .class="barrier:city_wall",                              },
	{ .class="barrier:ditch",                                  },
	{ .class="barrier:fence",                                  },
	{ .class="barrier:guard_rail",                             },
	{ .class="barrier:handrail",                               },
	{ .class="barrier:hedge",                                  },
	{ .class="barrier:kerb",                                   },
	{ .class="barrier:retaining_wall",                         },
	{ .class="barrier:tank_trap",                              },
	{ .class="barrier:wall",                                   },
	{ .class="barrier:block",                                  },
	{ .class="barrier:bollard",                                },
	{ .class="barrier:border_control",                         },
	{ .class="barrier:bump_gate",                              },
	{ .class="barrier:bus_trap",                               },
	{ .class="barrier:cattle_grid",                            },
	{ .class="barrier:chain",                                  },
	{ .class="barrier:cycle_barrier",                          },
	{ .class="barrier:debris",                                 },
	{ .class="barrier:entrance",                               },
	{ .class="barrier:full-height_turnstyle",                  },
	{ .class="barrier:gate",                                   },
	{ .class="barrier:hampshire_gate",                         },
	{ .class="barrier:height_restrictor",                      },
	{ .class="barrier:horse_stile",                            },
	{ .class="barrier:jersey_barrier",                         },
	{ .class="barrier:kent_carriage_gap",                      },
	{ .class="barrier:kissing_gate",                           },
	{ .class="barrier:lift_gate",                              },
	{ .class="barrier:log",                                    },
	{ .class="barrier:motorcycle_barrier",                     },
	{ .class="barrier:rope",                                   },
	{ .class="barrier:sally_port",                             },
	{ .class="barrier:spikes",                                 },
	{ .class="barrier:stile",                                  },
	{ .class="barrier:sump_buster",                            },
	{ .class="barrier:swing_gate",                             },
	{ .class="barrier:toll_booth",                             },
	{ .class="barrier:turnstile",                              },
	{ .class="barrier:yes",                                    },
	{ .boundary=1, .class="boundary:administrative",           },
	{ .boundary=1, .class="boundary:historic",                 },
	{ .boundary=1, .class="boundary:maritime",                 },
	{ .boundary=1, .class="boundary:national_park",            },
	{ .boundary=1, .class="boundary:political",                },
	{ .boundary=1, .class="boundary:postal_code",              },
	{ .boundary=1, .class="boundary:religious_administration", },
	{ .boundary=1, .class="boundary:protected_area",           },
	{ .building=1, .class="building:apartments",               },
	{ .building=1, .class="building:farm",                     },
	{ .building=1, .class="building:hotel",                    },
	{ .building=1, .class="building:house",                    },
	{ .building=1, .class="building:detached",                 },
	{ .building=1, .class="building:residential",              },
	{ .building=1, .class="building:dormatory",                },
	{ .building=1, .class="building:terrace",                  },
	{ .building=1, .class="building:houseboat",                },
	{ .building=1, .class="building:bungalow",                 },
	{ .building=1, .class="building:static_caravan",           },
	{ .building=1, .class="building:cabin",                    },
	{ .building=1, .class="building:commercial",               },
	{ .building=1, .class="building:office",                   },
	{ .building=1, .class="building:industrial",               },
	{ .building=1, .class="building:retail",                   },
	{ .building=1, .class="building:warehouse",                },
	{ .building=1, .class="building:kiosk",                    },
	{ .building=1, .class="building:religious",                },
	{ .building=1, .class="building:cathedral",                },
	{ .building=1, .class="building:chapel",                   },
	{ .building=1, .class="building:church",                   },
	{ .building=1, .class="building:mosque",                   },
	{ .building=1, .class="building:temple",                   },
	{ .building=1, .class="building:synagogue",                },
	{ .building=1, .class="building:shrine",                   },
	{ .building=1, .class="building:bakehouse",                },
	{ .building=1, .class="building:kindergarten",             },
	{ .building=1, .class="building:civic",                    },
	{ .building=1, .class="building:hospital",                 },
	{ .building=1, .class="building:school",                   },
	{ .building=1, .class="building:stadium",                  },
	{ .building=1, .class="building:train_station",            },
	{ .building=1, .class="building:transportation",           },
	{ .building=1, .class="building:university",               },
	{ .building=1, .class="building:grandstand",               },
	{ .building=1, .class="building:public",                   },
	{ .building=1, .class="building:barn",                     },
	{ .building=1, .class="building:bridge",                   },
	{ .building=1, .class="building:bunker",                   },
	{ .building=1, .class="building:carport",                  },
	{ .building=1, .class="building:conservatory",             },
	{ .building=1, .class="building:construction",             },
	{ .building=1, .class="building:cowshed",                  },
	{ .building=1, .class="building:digester",                 },
	{ .building=1, .class="building:farm_auxilary",            },
	{ .building=1, .class="building:garage",                   },
	{ .building=1, .class="building:garages",                  },
	{ .building=1, .class="building:garbage_shed",             },
	{ .building=1, .class="building:greenhouse",               },
	{ .building=1, .class="building:hangar",                   },
	{ .building=1, .class="building:hut",                      },
	{ .building=1, .class="building:pavilion",                 },
	{ .building=1, .class="building:parking",                  },
	{ .building=1, .class="building:riding_hall",              },
	{ .building=1, .class="building:roof",                     },
	{ .building=1, .class="building:shed",                     },
	{ .building=1, .class="building:sports_hall",              },
	{ .building=1, .class="building:stable",                   },
	{ .building=1, .class="building:sty",                      },
	{ .building=1, .class="building:transformer_tower",        },
	{ .building=1, .class="building:service",                  },
	{ .building=1, .class="building:ruins",                    },
	{ .building=1, .class="building:water_tower",              },
	{ .building=1, .class="building:yes",                      },
	{ .class="craft:agricultural_engines",                     },
	{ .class="craft:bakery",                                   },
	{ .class="craft:basket_maker",                             },
	{ .class="craft:beekeeper",                                },
	{ .class="craft:blacksmith",                               },
	{ .class="craft:boatbuilder",                              },
	{ .class="craft:bookbinder",                               },
	{ .class="craft:brewery",                                  },
	{ .class="craft:builder",                                  },
	{ .class="craft:carpenter",                                },
	{ .class="craft:carpet_layer",                             },
	{ .class="craft:caterer",                                  },
	{ .class="craft:chimney_sweeper",                          },
	{ .class="craft:clockmaker",                               },
	{ .class="craft:confectionery",                            },
	{ .class="craft:dental_technican",                         },
	{ .class="craft:distillery",                               },
	{ .class="craft:dressmaker",                               },
	{ .class="craft:embroiderer",                              },
	{ .class="craft:electrician",                              },
	{ .class="craft:engraver",                                 },
	{ .class="craft:floorer",                                  },
	{ .class="craft:gardener",                                 },
	{ .class="craft:glaziery",                                 },
	{ .class="craft:handicraft",                               },
	{ .class="craft:hvac",                                     },
	{ .class="craft:insulation",                               },
	{ .class="craft:jeweller",                                 },
	{ .class="craft:joiner",                                   },
	{ .class="craft:key_cutter",                               },
	{ .class="craft:locksmith",                                },
	{ .class="craft:metal_construction",                       },
	{ .class="craft:mint",                                     },
	{ .class="craft:optician",                                 },
	{ .class="craft:painter",                                  },
	{ .class="craft:photographer",                             },
	{ .class="craft:photographic_laboratory",                  },
	{ .class="craft:piano_tuner",                              },
	{ .class="craft:plasterer",                                },
	{ .class="craft:plumber",                                  },
	{ .class="craft:pottery",                                  },
	{ .class="craft:printmaker",                               },
	{ .class="craft:rigger",                                   },
	{ .class="craft:roofer",                                   },
	{ .class="craft:saddler",                                  },
	{ .class="craft:sailmaker",                                },
	{ .class="craft:sawmill",                                  },
	{ .class="craft:scaffolder",                               },
	{ .class="craft:sculpter",                                 },
	{ .class="craft:shoemaker",                                },
	{ .class="craft:stand_builder",                            },
	{ .class="craft:stonemason",                               },
	{ .class="craft:sun_protection",                           },
	{ .class="craft:tailor",                                   },
	{ .class="craft:tiler",                                    },
	{ .class="craft:tinsmith",                                 },
	{ .class="craft:toolmaker",                                },
	{ .class="craft:turner",                                   },
	{ .class="craft:upholsterer",                              },
	{ .class="craft:watchmaker",                               },
	{ .class="craft:window_construction",                      },
	{ .class="craft:winery",                                   },
	{ .class="emergency:ambulance_station",                    },
	{ .class="emergency:defibrillator",                        },
	{ .class="emergency:first_aid_kit",                        },
	{ .class="emergency:landing_site",                         },
	{ .class="emergency:emergency_ward_entrance",              },
	{ .class="emergency:dry_riser_inlet",                      },
	{ .class="emergency:fire_alarm_box",                       },
	{ .class="emergency:fire_extinguisher",                    },
	{ .class="emergency:fire_flapper",                         },
	{ .class="emergency:fire_hose",                            },
	{ .class="emergency:fire_hydrant",                         },
	{ .class="emergency:water_tank",                           },
	{ .class="emergency:fire_water_pond",                      },
	{ .class="emergency:suction_point",                        },
	{ .class="emergency:lifeguard",                            },
	{ .class="emergency:lifeguard_base",                       },
	{ .class="emergency:lifeguard_tower",                      },
	{ .class="emergency:lifeguard_platform",                   },
	{ .class="emergency:life_ring",                            },
	{ .class="emergency:mountain_rescue",                      },
	{ .class="emergency:ses_station",                          },
	{ .class="emergency:assembly_point",                       },
	{ .class="emergency:access_point",                         },
	{ .class="emergency:phone",                                },
	{ .class="emergency:rescue_box",                           },
	{ .class="emergency:siren",                                },
	{ .class="geological:moraine",                             },
	{ .class="geological:outcrop",                             },
	{ .class="geological:palaeontological_site",               },
	{ .class="highway:motorway",                               },
	{ .class="highway:trunk",                                  },
	{ .class="highway:primary",                                },
	{ .class="highway:secondary",                              },
	{ .class="highway:tertiary",                               },
	{ .class="highway:unclassified",                           },
	{ .class="highway:residential",                            },
	{ .class="highway:service",                                },
	{ .class="highway:motorway_link",                          },
	{ .class="highway:trunk_link",                             },
	{ .class="highway:primary_link",                           },
	{ .class="highway:secondary_link",                         },
	{ .class="highway:tertiary_link",                          },
	{ .class="highway:living_street",                          },
	{ .class="highway:pedestrian",                             },
	{ .class="highway:track",                                  },
	{ .class="highway:bus_guideway",                           },
	{ .class="highway:escape",                                 },
	{ .class="highway:raceway",                                },
	{ .class="highway:road",                                   },
	{ .class="highway:footway",                                },
	{ .class="highway:bridleway",                              },
	{ .class="highway:steps",                                  },
	{ .class="highway:path",                                   },
	{ .class="highway:cycleway",                               },
	{ .class="highway:bus_stop",                               },
	{ .class="highway:crossing",                               },
	{ .class="highway:elevator",                               },
	{ .class="highway:emergency_access_point",                 },
	{ .class="highway:give_way",                               },
	{ .class="highway:mini_roundabout",                        },
	{ .class="highway:motorway_junction",                      },
	{ .class="highway:passing_place",                          },
	{ .class="highway:rest_area",                              },
	{ .class="highway:speed_camera",                           },
	{ .class="highway:street_lamp",                            },
	{ .class="highway:services",                               },
	{ .class="highway:stop",                                   },
	{ .class="highway:traffic_signals",                        },
	{ .class="highway:turning_circle",                         },
	{ .class="historic:aircraft",                              },
	{ .class="historic:aqueduct",                              },
	{ .class="historic:archaeological_site",                   },
	{ .class="historic:battlefield",                           },
	{ .class="historic:boundary_stone",                        },
	{ .building=1, .class="historic:building",                 },
	{ .class="historic:cannon",                                },
	{ .class="historic:castle",                                },
	{ .class="historic:castle_wall",                           },
	{ .class="historic:church",                                },
	{ .class="historic:city_gate",                             },
	{ .class="historic:citywalls",                             },
	{ .class="historic:farm",                                  },
	{ .class="historic:fort",                                  },
	{ .class="historic:gallows",                               },
	{ .class="historic:highwater_mark",                        },
	{ .class="historic:locomotive",                            },
	{ .class="historic:manor",                                 },
	{ .class="historic:memorial",                              },
	{ .class="historic:milestone",                             },
	{ .class="historic:monastery",                             },
	{ .class="historic:monument",                              },
	{ .class="historic:optical_telegraph",                     },
	{ .class="historic:pillory",                               },
	{ .class="historic:railway_car",                           },
	{ .class="historic:ruins",                                 },
	{ .class="historic:rune_stone",                            },
	{ .class="historic:ship",                                  },
	{ .class="historic:tomb",                                  },
	{ .class="historic:tower",                                 },
	{ .class="historic:wayside_cross",                         },
	{ .class="historic:wayside_shrine",                        },
	{ .class="historic:wreck",                                 },
	{ .class="historic:yes",                                   },
	{ .class="landuse:commercial",                             },
	{ .class="landuse:construction",                           },
	{ .class="landuse:industrial",                             },
	{ .class="landuse:residential",                            },
	{ .class="landuse:retail",                                 },
	{ .class="landuse:allotments",                             },
	{ .class="landuse:basin",                                  },
	{ .class="landuse:brownfield",                             },
	{ .class="landuse:cemetery",                               },
	{ .class="landuse:depot",                                  },
	{ .class="landuse:farmland",                               },
	{ .class="landuse:farmyard",                               },
	{ .class="landuse:forest",                                 },
	{ .class="landuse:garages",                                },
	{ .class="landuse:grass",                                  },
	{ .class="landuse:greenfield",                             },
	{ .class="landuse:greenhouse_horticulture",                },
	{ .class="landuse:landfill",                               },
	{ .class="landuse:meadow",                                 },
	{ .class="landuse:military",                               },
	{ .class="landuse:orchard",                                },
	{ .class="landuse:plant_nursery",                          },
	{ .class="landuse:port",                                   },
	{ .class="landuse:quarry",                                 },
	{ .class="landuse:railway",                                },
	{ .class="landuse:recreation_ground",                      },
	{ .class="landuse:winter_sports",                          },
	{ .class="landuse:religious",                              },
	{ .class="landuse:reservoir",                              },
	{ .class="landuse:salt_pond",                              },
	{ .class="landuse:village_green",                          },
	{ .class="landuse:vineyard",                               },
	{ .class="leisure:adult_gaming_centre",                    },
	{ .class="leisure:amusement_arcade",                       },
	{ .class="leisure:beach_resort",                           },
	{ .class="leisure:bandstand",                              },
	{ .class="leisure:bird_hide",                              },
	{ .class="leisure:common",                                 },
	{ .class="leisure:dance",                                  },
	{ .class="leisure:disc_golf_course",                       },
	{ .class="leisure:dog_park",                               },
	{ .class="leisure:escape_game",                            },
	{ .class="leisure:firepit",                                },
	{ .class="leisure:fishing",                                },
	{ .class="leisure:fitness_centre",                         },
	{ .class="leisure:fitness_station",                        },
	{ .class="leisure:garden",                                 },
	{ .class="leisure:hackerspace",                            },
	{ .class="leisure:horse_riding",                           },
	{ .class="leisure:ice_rink",                               },
	{ .class="leisure:marina",                                 },
	{ .class="leisure:minature_golf",                          },
	{ .class="leisure:nature_reserve",                         },
	{ .class="leisure:park",                                   },
	{ .class="leisure:picnic_table",                           },
	{ .class="leisure:pitch",                                  },
	{ .class="leisure:playground",                             },
	{ .class="leisure:slipway",                                },
	{ .class="leisure:sports_centre",                          },
	{ .class="leisure:stadium",                                },
	{ .class="leisure:summer_camp",                            },
	{ .class="leisure:swimming_area",                          },
	{ .class="leisure:swimming_pool",                          },
	{ .class="leisure:track",                                  },
	{ .class="leisure:water_park",                             },
	{ .class="leisure:wildlife_hide",                          },
	{ .class="man_made:adit",                                  },
	{ .class="man_made:beacon",                                },
	{ .class="man_made:breakwater",                            },
	{ .class="man_made:bridge",                                },
	{ .class="man_made:bunker_silo",                           },
	{ .class="man_made:campanile",                             },
	{ .class="man_made:chimney",                               },
	{ .class="man_made:communications_tower",                  },
	{ .class="man_made:crane",                                 },
	{ .class="man_made:cross",                                 },
	{ .class="man_made:cutline",                               },
	{ .class="man_made:clearcut",                              },
	{ .class="man_made:dovecote",                              },
	{ .class="man_made:drinking_fountain",                     },
	{ .class="man_made:dyke",                                  },
	{ .class="man_made:embankment",                            },
	{ .class="man_made:flagpole",                              },
	{ .class="man_made:gasometer",                             },
	{ .class="man_made:groyne",                                },
	{ .class="man_made:guy",                                   },
	{ .class="man_made:kiln",                                  },
	{ .class="man_made:lighthouse",                            },
	{ .class="man_made:mast",                                  },
	{ .class="man_made:mineshaft",                             },
	{ .class="man_made:monitoring_station",                    },
	{ .class="man_made:obelisk",                               },
	{ .class="man_made:observatory",                           },
	{ .class="man_made:offshore_platform",                     },
	{ .class="man_made:petroleum_well",                        },
	{ .class="man_made:pier",                                  },
	{ .class="man_made:pipeline",                              },
	{ .class="man_made:pumping_station",                       },
	{ .class="man_made:reservoir_covered",                     },
	{ .class="man_made:silo",                                  },
	{ .class="man_made:snow_fence",                            },
	{ .class="man_made:snow_net",                              },
	{ .class="man_made:storage_tank",                          },
	{ .class="man_made:street_cabinet",                        },
	{ .class="man_made:surveillance",                          },
	{ .class="man_made:survey_point",                          },
	{ .class="man_made:telescope",                             },
	{ .class="man_made:tower",                                 },
	{ .class="man_made:wastewater_plant",                      },
	{ .class="man_made:watermill",                             },
	{ .class="man_made:water_tower",                           },
	{ .class="man_made:water_well",                            },
	{ .class="man_made:water_tap",                             },
	{ .class="man_made:water_works",                           },
	{ .class="man_made:wildlife_crossing",                     },
	{ .class="man_made:windmill",                              },
	{ .class="man_made:works",                                 },
	{ .class="man_made:yes",                                   },
	{ .class="military:airfield",                              },
	{ .class="military:ammunition",                            },
	{ .class="military:bunker",                                },
	{ .class="military:barracks",                              },
	{ .class="military:checkpoint",                            },
	{ .class="military:danger_area",                           },
	{ .class="military:naval_base",                            },
	{ .class="military:nuclear_explosion_site",                },
	{ .class="military:obstacle_course",                       },
	{ .class="military:office",                                },
	{ .class="military:range",                                 },
	{ .class="military:training_area",                         },
	{ .class="military:trench",                                },
	{ .class="military:launchpad",                             },
	{ .class="natural:wood",                                   },
	{ .class="natural:tree_row",                               },
	{ .class="natural:tree",                                   },
	{ .class="natural:scrub",                                  },
	{ .class="natural:heath",                                  },
	{ .class="natural:moor",                                   },
	{ .class="natural:grassland",                              },
	{ .class="natural:fell",                                   },
	{ .class="natural:bare_rock",                              },
	{ .class="natural:scree",                                  },
	{ .class="natural:shingle",                                },
	{ .class="natural:sand",                                   },
	{ .class="natural:mud",                                    },
	{ .class="natural:water",                                  },
	{ .class="natural:wetland",                                },
	{ .class="natural:glacier",                                },
	{ .class="natural:bay",                                    },
	{ .class="natural:cape",                                   },
	{ .class="natural:beach",                                  },
	{ .class="natural:coastline",                              },
	{ .class="natural:spring",                                 },
	{ .class="natural:hot_spring",                             },
	{ .class="natural:geyser",                                 },
	{ .class="natural:blowhole",                               },
	{ .class="natural:peak",                                   },
	{ .class="natural:volcano",                                },
	{ .class="natural:valley",                                 },
	{ .class="natural:ridge",                                  },
	{ .class="natural:arete",                                  },
	{ .class="natural:cliff",                                  },
	{ .class="natural:saddle",                                 },
	{ .class="natural:rock",                                   },
	{ .class="natural:stone",                                  },
	{ .class="natural:sinkhole",                               },
	{ .class="natural:cave_entrance",                          },
	{ .class="office:accountant",                              },
	{ .class="office:adoption_agency",                         },
	{ .class="office:advertising_agency",                      },
	{ .class="office:architect",                               },
	{ .class="office:association",                             },
	{ .class="office:charity",                                 },
	{ .class="office:company",                                 },
	{ .class="office:educational_institution",                 },
	{ .class="office:employment_agency",                       },
	{ .class="office:energy_supplier",                         },
	{ .class="office:estate_agent",                            },
	{ .class="office:forestry",                                },
	{ .class="office:foundation",                              },
	{ .class="office:government",                              },
	{ .class="office:guide",                                   },
	{ .class="office:healer",                                  },
	{ .class="office:insurance",                               },
	{ .class="office:it",                                      },
	{ .class="office:lawyer",                                  },
	{ .class="office:logistics",                               },
	{ .class="office:moving_company",                          },
	{ .class="office:newspaper",                               },
	{ .class="office:ngo",                                     },
	{ .class="office:notary",                                  },
	{ .class="office:physican",                                },
	{ .class="office:political_party",                         },
	{ .class="office:private_investigator",                    },
	{ .class="office:property_management",                     },
	{ .class="office:quango",                                  },
	{ .class="office:real_estate_agent",                       },
	{ .class="office:religion",                                },
	{ .class="office:research",                                },
	{ .class="office:surveyor",                                },
	{ .class="office:tax",                                     },
	{ .class="office:tax_advisor",                             },
	{ .class="office:telecommunication",                       },
	{ .class="office:therapist",                               },
	{ .class="office:travel_agent",                            },
	{ .class="office:water_utility",                           },
	{ .class="office:yes",                                     },
	{ .class="place:country",                                  },
	{ .class="place:state",                                    },
	{ .class="place:region",                                   },
	{ .class="place:province",                                 },
	{ .class="place:district",                                 },
	{ .class="place:county",                                   },
	{ .class="place:municipality",                             },
	{ .class="place:city",                                     },
	{ .class="place:borough",                                  },
	{ .class="place:suburb",                                   },
	{ .class="place:quarter",                                  },
	{ .class="place:neighbourhood",                            },
	{ .class="place:city_block",                               },
	{ .class="place:plot",                                     },
	{ .class="place:town",                                     },
	{ .class="place:village",                                  },
	{ .class="place:hamlet",                                   },
	{ .class="place:isolated_dwelling",                        },
	{ .class="place:farm",                                     },
	{ .class="place:allotments",                               },
	{ .class="place:continent",                                },
	{ .class="place:archipelago",                              },
	{ .class="place:island",                                   },
	{ .class="place:islet",                                    },
	{ .class="place:square",                                   },
	{ .class="place:locality",                                 },
	{ .class="power:plant",                                    },
	{ .class="power:cable",                                    },
	{ .class="power:compensator",                              },
	{ .class="power:convertor",                                },
	{ .class="power:generator",                                },
	{ .class="power:heliostat",                                },
	{ .class="power:insulator",                                },
	{ .class="power:line",                                     },
	{ .class="line:busbar",                                    },
	{ .class="line:bay",                                       },
	{ .class="power:minor_line",                               },
	{ .class="power:pole",                                     },
	{ .class="power:portal",                                   },
	{ .class="power:catenary_mast",                            },
	{ .class="power:substation",                               },
	{ .class="power:switch",                                   },
	{ .class="power:terminal",                                 },
	{ .class="power:tower",                                    },
	{ .class="power:transformer",                              },
	{ .class="public_transport:stop_position",                 },
	{ .class="public_transport:platform",                      },
	{ .class="public_transport:station",                       },
	{ .class="public_transport:stop_area",                     },
	{ .class="railway:abandoned",                              },
	{ .class="railway:construction",                           },
	{ .class="railway:disused",                                },
	{ .class="railway:funicular",                              },
	{ .class="railway:light_rail",                             },
	{ .class="railway:minature",                               },
	{ .class="railway:monorail",                               },
	{ .class="railway:narrow_gauge",                           },
	{ .class="railway:preserved",                              },
	{ .class="railway:rail",                                   },
	{ .class="railway:subway",                                 },
	{ .class="railway:tram",                                   },
	{ .class="railway:halt",                                   },
	{ .class="railway:platform",                               },
	{ .class="railway:station",                                },
	{ .class="railway:subway_entrance",                        },
	{ .class="railway:tram_stop",                              },
	{ .class="railway:buffer_stop",                            },
	{ .class="railway:derail",                                 },
	{ .class="railway:crossing",                               },
	{ .class="railway:level_crossing",                         },
	{ .class="railway:signal",                                 },
	{ .class="railway:switch",                                 },
	{ .class="railway:railway_crossing",                       },
	{ .class="railway:turntable",                              },
	{ .class="railway:roundhouse",                             },
	{ .class="railway:traverser",                              },
	{ .class="railway:wash",                                   },
	{ .class="shop:alcohol",                                   },
	{ .class="shop:bakery",                                    },
	{ .class="shop:beverages",                                 },
	{ .class="shop:brewing_supplies",                          },
	{ .class="shop:butcher",                                   },
	{ .class="shop:cheese",                                    },
	{ .class="shop:chocolate",                                 },
	{ .class="shop:coffee",                                    },
	{ .class="shop:confectionery",                             },
	{ .class="shop:convenience",                               },
	{ .class="shop:deli",                                      },
	{ .class="shop:dairy",                                     },
	{ .class="shop:farm",                                      },
	{ .class="shop:frozen_food",                               },
	{ .class="shop:greengrocier",                              },
	{ .class="shop:health_food",                               },
	{ .class="shop:ice_cream",                                 },
	{ .class="shop:pasta",                                     },
	{ .class="shop:pastry",                                    },
	{ .class="shop:seafood",                                   },
	{ .class="shop:spices",                                    },
	{ .class="shop:tea",                                       },
	{ .class="shop:water",                                     },
	{ .class="shop:department_store",                          },
	{ .class="shop:general",                                   },
	{ .class="shop:kiosk",                                     },
	{ .class="shop:mall",                                      },
	{ .class="shop:supermarket",                               },
	{ .class="shop:wholesale",                                 },
	{ .class="shop:baby_goods",                                },
	{ .class="shop:bag",                                       },
	{ .class="shop:boutique",                                  },
	{ .class="shop:clothes",                                   },
	{ .class="shop:fabric",                                    },
	{ .class="shop:fashion",                                   },
	{ .class="shop:jewelry",                                   },
	{ .class="shop:leather",                                   },
	{ .class="shop:sewing",                                    },
	{ .class="shop:shoes",                                     },
	{ .class="shop:tailor",                                    },
	{ .class="shop:watches",                                   },
	{ .class="shop:charity",                                   },
	{ .class="shop:second_hand",                               },
	{ .class="shop:variety_store",                             },
	{ .class="shop:beauty",                                    },
	{ .class="shop:chemist",                                   },
	{ .class="shop:cosmetics",                                 },
	{ .class="shop:erotic",                                    },
	{ .class="shop:hairdresser",                               },
	{ .class="shop:hairdresser_suply",                         },
	{ .class="shop:hearing_aids",                              },
	{ .class="shop:herbalist",                                 },
	{ .class="shop:massage",                                   },
	{ .class="shop:medical_supply",                            },
	{ .class="shop:nutrition_supplements",                     },
	{ .class="shop:optician",                                  },
	{ .class="shop:perfumery",                                 },
	{ .class="shop:tattoo",                                    },
	{ .class="shop:agrarian",                                  },
	{ .class="shop:appliance",                                 },
	{ .class="shop:bathroom_furnishing",                       },
	{ .class="shop:doityourself",                              },
	{ .class="shop:electrical",                                },
	{ .class="shop:energy",                                    },
	{ .class="shop:fireplace",                                 },
	{ .class="shop:florist",                                   },
	{ .class="shop:garden_centre",                             },
	{ .class="shop:garden_furniture",                          },
	{ .class="shop:gas",                                       },
	{ .class="shop:glaziery",                                  },
	{ .class="shop:hardware",                                  },
	{ .class="shop:houseware",                                 },
	{ .class="shop:locksmith",                                 },
	{ .class="shop:paint",                                     },
	{ .class="shop:security",                                  },
	{ .class="shop:trade",                                     },
	{ .class="shop:antiques",                                  },
	{ .class="shop:bed",                                       },
	{ .class="shop:candles",                                   },
	{ .class="shop:carpet",                                    },
	{ .class="shop:curtain",                                   },
	{ .class="shop:doors",                                     },
	{ .class="shop:flooring",                                  },
	{ .class="shop:furniture",                                 },
	{ .class="shop:interior_decoration",                       },
	{ .class="shop:kitchen",                                   },
	{ .class="shop:lamps",                                     },
	{ .class="shop:tiles",                                     },
	{ .class="shop:window_blind",                              },
	{ .class="shop:computer",                                  },
	{ .class="shop:robot",                                     },
	{ .class="shop:electronics",                               },
	{ .class="shop:hifi",                                      },
	{ .class="shop:mobile_phone",                              },
	{ .class="shop:radiotechnics",                             },
	{ .class="shop:vacuum_cleaner",                            },
	{ .class="shop:atv",                                       },
	{ .class="shop:bicycle",                                   },
	{ .class="shop:boat",                                      },
	{ .class="shop:car",                                       },
	{ .class="shop:car_repair",                                },
	{ .class="shop:car_parts",                                 },
	{ .class="shop:fuel",                                      },
	{ .class="shop:fishing",                                   },
	{ .class="shop:free_flying",                               },
	{ .class="shop:hunting",                                   },
	{ .class="shop:jetski",                                    },
	{ .class="shop:motorcycle",                                },
	{ .class="shop:outdoor",                                   },
	{ .class="shop:scuba_diving",                              },
	{ .class="shop:ski",                                       },
	{ .class="shop:snowmobile",                                },
	{ .class="shop:sports",                                    },
	{ .class="shop:swimming_pool",                             },
	{ .class="shop:tyres",                                     },
	{ .class="shop:art",                                       },
	{ .class="shop:collector",                                 },
	{ .class="shop:craft",                                     },
	{ .class="shop:frame",                                     },
	{ .class="shop:games",                                     },
	{ .class="shop:model",                                     },
	{ .class="shop:music",                                     },
	{ .class="shop:musical_instrument",                        },
	{ .class="shop:photo",                                     },
	{ .class="shop:camera",                                    },
	{ .class="shop:trophy",                                    },
	{ .class="shop:video",                                     },
	{ .class="shop:video_games",                               },
	{ .class="shop:anime",                                     },
	{ .class="shop:books",                                     },
	{ .class="shop:gift",                                      },
	{ .class="shop:lottery",                                   },
	{ .class="shop:newsagent",                                 },
	{ .class="shop:stationery",                                },
	{ .class="shop:ticket",                                    },
	{ .class="shop:bookmaker",                                 },
	{ .class="shop:copyshop",                                  },
	{ .class="shop:dry_cleaning",                              },
	{ .class="shop:e-cigarette",                               },
	{ .class="shop:funeral_directors",                         },
	{ .class="shop:laundry",                                   },
	{ .class="shop:money_lender",                              },
	{ .class="shop:party",                                     },
	{ .class="shop:pawnbroker",                                },
	{ .class="shop:pet",                                       },
	{ .class="shop:pyrotechnics",                              },
	{ .class="shop:religion",                                  },
	{ .class="shop:storage_rental",                            },
	{ .class="shop:tobacco",                                   },
	{ .class="shop:toys",                                      },
	{ .class="shop:travel_agency",                             },
	{ .class="shop:vacant",                                    },
	{ .class="shop:weapons",                                   },
	{ .class="sport:9pin",                                     },
	{ .class="sport:10pin",                                    },
	{ .class="sport:american_football",                        },
	{ .class="sport:aikido",                                   },
	{ .class="sport:archery",                                  },
	{ .class="sport:athletics",                                },
	{ .class="sport:australian_football",                      },
	{ .class="sport:badminton",                                },
	{ .class="sport:bandy",                                    },
	{ .class="sport:base",                                     },
	{ .class="sport:baseball",                                 },
	{ .class="sport:basketball",                               },
	{ .class="sport:beachvolleyball",                          },
	{ .class="sport:billards",                                 },
	{ .class="sport:bmx",                                      },
	{ .class="sport:bobsleigh",                                },
	{ .class="sport:boules",                                   },
	{ .class="sport:bowls",                                    },
	{ .class="sport:boxing",                                   },
	{ .class="sport:canadian_football",                        },
	{ .class="sport:canoe",                                    },
	{ .class="sport:chess",                                    },
	{ .class="sport:cliff_diving",                             },
	{ .class="sport:climbing",                                 },
	{ .class="sport:climbing_adventure",                       },
	{ .class="sport:cricket",                                  },
	{ .class="sport:croquet",                                  },
	{ .class="sport:curling",                                  },
	{ .class="sport:cycling",                                  },
	{ .class="sport:darts",                                    },
	{ .class="sport:dog_racing",                               },
	{ .class="sport:equestrian",                               },
	{ .class="sport:fencing",                                  },
	{ .class="sport:field_hockey",                             },
	{ .class="sport:free_flying",                              },
	{ .class="sport:futsal",                                   },
	{ .class="sport:gaelic_games",                             },
	{ .class="sport:golf",                                     },
	{ .class="sport:gymnastics",                               },
	{ .class="sport:handball",                                 },
	{ .class="sport:hapkido",                                  },
	{ .class="sport:horseshoes",                               },
	{ .class="sport:horse_racing",                             },
	{ .class="sport:ice_hockey",                               },
	{ .class="sport:ice_skating",                              },
	{ .class="sport:ice_stock",                                },
	{ .class="sport:judo",                                     },
	{ .class="sport:karate",                                   },
	{ .class="sport:karting",                                  },
	{ .class="sport:kitesurfing",                              },
	{ .class="sport:korfball",                                 },
	{ .class="sport:lacrosse",                                 },
	{ .class="sport:model_aerodrome",                          },
	{ .class="sport:motocross",                                },
	{ .class="sport:motor",                                    },
	{ .class="sport:multi",                                    },
	{ .class="sport:netball",                                  },
	{ .class="sport:obstacle_course",                          },
	{ .class="sport:orienteering",                             },
	{ .class="sport:paddle_tennis",                            },
	{ .class="sport:padel",                                    },
	{ .class="sport:parachuting",                              },
	{ .class="sport:paragliding",                              },
	{ .class="sport:pelota",                                   },
	{ .class="sport:racquet",                                  },
	{ .class="sport:rc_car",                                   },
	{ .class="sport:roller_skating",                           },
	{ .class="sport:rowing",                                   },
	{ .class="sport:rugby_league",                             },
	{ .class="sport:rugby_union",                              },
	{ .class="sport:running",                                  },
	{ .class="sport:sailing",                                  },
	{ .class="sport:scuba_diving",                             },
	{ .class="sport:shooting",                                 },
	{ .class="sport:skateboard",                               },
	{ .class="sport:soccer",                                   },
	{ .class="sport:sumo",                                     },
	{ .class="sport:surfing",                                  },
	{ .class="sport:swimming",                                 },
	{ .class="sport:table_tennis",                             },
	{ .class="sport:table_soccer",                             },
	{ .class="sport:taekwondo",                                },
	{ .class="sport:tennis",                                   },
	{ .class="sport:toboggan",                                 },
	{ .class="sport:volleyball",                               },
	{ .class="sport:water_polo",                               },
	{ .class="sport:water_ski",                                },
	{ .class="sport:weightlifting",                            },
	{ .class="sport:wrestling",                                },
	{ .class="sport:yoga",                                     },
	{ .class="tourism:alpine_hut",                             },
	{ .class="tourism:apartment",                              },
	{ .class="tourism:aquarium",                               },
	{ .class="tourism:artwork",                                },
	{ .class="tourism:attraction",                             },
	{ .class="tourism:camp_site",                              },
	{ .class="tourism:caravan_site",                           },
	{ .class="tourism:chalet",                                 },
	{ .class="tourism:gallery",                                },
	{ .class="tourism:guest_house",                            },
	{ .class="tourism:hostel",                                 },
	{ .class="tourism:hotel",                                  },
	{ .class="tourism:information",                            },
	{ .class="tourism:motel",                                  },
	{ .class="tourism:museum",                                 },
	{ .class="tourism:picnic_site",                            },
	{ .class="tourism:theme_park",                             },
	{ .class="tourism:viewpoint",                              },
	{ .class="tourism:wilderness_hut",                         },
	{ .class="tourism:zoo",                                    },
	{ .class="tourism:yes",                                    },
	{ .class="waterway:river",                                 },
	{ .class="waterway:riverbank",                             },
	{ .class="waterway:stream",                                },
	{ .class="waterway:wadi",                                  },
	{ .class="waterway:drystream",                             },
	{ .class="waterway:canal",                                 },
	{ .class="waterway:drain",                                 },
	{ .class="waterway:ditch",                                 },
	{ .class="waterway:fairway",                               },
	{ .class="waterway:dock",                                  },
	{ .class="waterway:boatyard",                              },
	{ .class="waterway:dam",                                   },
	{ .class="waterway:weir",                                  },
	{ .class="waterway:stream_end",                            },
	{ .class="waterway:waterfall",                             },
	{ .class="waterway:lock_gate",                             },
	{ .class="waterway:turning_point",                         },
	{ .class="waterway:water_point",                           },
	{ .class="waterway:fuel",                                  },
	{ .core=1, .boundary=1, .class="core:wilderness",          },
	{ .core=1, .boundary=1, .class="core:recreation",          },
	{ .core=1, .boundary=1, .class="core:special",             },
	{ .core=1, .boundary=1, .class="core:mineral",             },
	{ .class="default:waypoint",                               },
	{ .core=1, .boundary=1, .class="core:coal_methane",        },
	{ .core=1, .boundary=1, .class="core:historic",            },
	{ .class="rec:wilderness",                                 },
	{ .class="rec:special",                                    },
	{ .class="rec:mineral",                                    },
	{ .class="craft:parquet_layer",                            },
	{ .boundary=1, .class="boundary:state"                     },
	{ .boundary=1, .class="boundary:national_park2"            },
	{ .boundary=1, .class="boundary:national_monument3"        },
	{ .class=NULL                                              },
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
	ASSERT(name);

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
	ASSERT(abrev);

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
	if((code >= 0) && (code < 60))
	{
		return OSM_UTIL_ST[code].state;
	}
	return NULL;
}

const char* osmdb_stCodeToAbrev(int code)
{
	if((code >= 0) && (code < 60))
	{
		return OSM_UTIL_ST[code].st;
	}
	return NULL;
}

int osmdb_classNameToCode(const char* name)
{
	ASSERT(name);

	int idx = 0;
	while(OSM_UTIL_CLASSES[idx].class)
	{
		if(strcmp(OSM_UTIL_CLASSES[idx].class, name) == 0)
		{
			return idx;
		}

		++idx;
	}

	return 0;
}

int osmdb_classKVToCode(const char* k, const char* v)
{
	ASSERT(k);
	ASSERT(v);

	char name[256];
	snprintf(name, 256, "%s:%s", k, v);
	return osmdb_classNameToCode(name);
}

const char* osmdb_classCodeToName(int code)
{
	int count = osmdb_classCount();
	if(code < count)
	{
		return OSM_UTIL_CLASSES[code].class;
	}
	return OSM_UTIL_CLASSES[0].class;
}

int osmdb_classIsBuilding(int code)
{
	int count = osmdb_classCount();
	if(code < count)
	{
		return OSM_UTIL_CLASSES[code].building;
	}
	return OSM_UTIL_CLASSES[0].building;
}

int osmdb_classIsBoundary(int code)
{
	int count = osmdb_classCount();
	if(code < count)
	{
		return OSM_UTIL_CLASSES[code].boundary;
	}
	return OSM_UTIL_CLASSES[0].boundary;
}

int osmdb_classIsCore(int code)
{
	int count = osmdb_classCount();
	if(code < count)
	{
		return OSM_UTIL_CLASSES[code].core;
	}
	return OSM_UTIL_CLASSES[0].core;
}

int osmdb_classCount(void)
{
	// note that the last class is NULL
	return sizeof(OSM_UTIL_CLASSES)/
	       sizeof(osmdb_utilClass_t) - 1;
}

int osmdb_relationTagTypeToCode(const char* type)
{
	ASSERT(type);

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
	// note that the last item is NULL
	int count = sizeof(OSM_UTIL_RELATION_TAG_TYPE)/
	            sizeof(char*) - 1;
	if(code < count)
	{
		return OSM_UTIL_RELATION_TAG_TYPE[code];
	}
	return OSM_UTIL_RELATION_TAG_TYPE[0];
}

int osmdb_relationMemberTypeToCode(const char* type)
{
	ASSERT(type);

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
	// note that the last item is NULL
	int count = sizeof(OSM_UTIL_RELATION_MEMBER_TYPE)/
	            sizeof(char*) - 1;
	if(code < count)
	{
		return OSM_UTIL_RELATION_MEMBER_TYPE[code];
	}
	return OSM_UTIL_RELATION_MEMBER_TYPE[0];
}

int osmdb_relationMemberRoleToCode(const char* role)
{
	ASSERT(role);

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
	// note that the last item is NULL
	int count = sizeof(OSM_UTIL_RELATION_MEMBER_ROLE)/
	            sizeof(char*) - 1;
	if(code < count)
	{
		return OSM_UTIL_RELATION_MEMBER_ROLE[code];
	}
	return OSM_UTIL_RELATION_MEMBER_ROLE[0];
}

int osmdb_fileExists(const char* fname)
{
	ASSERT(fname);

	if(access(fname, F_OK) != 0)
	{
		return 0;
	}
	return 1;
}

int osmdb_mkdir(const char* path)
{
	ASSERT(path);

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
			if(mkdir(dir, S_IRWXU | S_IRWXG | S_IROTH |
			              S_IXOTH) == -1)
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

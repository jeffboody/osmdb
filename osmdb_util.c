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
	int         rank;
	int         building;
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
osmdb_utilClass_t OSM_UTIL_CLASSES[] =
{
	{ 0, 0, "class:none",                                  },
	{ 0, 0, "aerialway:cable_car",                         },
	{ 0, 0, "aerialway:gondola",                           },
	{ 0, 0, "aerialway:chair_lift",                        },
	{ 0, 0, "aerialway:mixed_lift",                        },
	{ 0, 0, "aerialway:drag_lift",                         },
	{ 0, 0, "aerialway:t-bar",                             },
	{ 0, 0, "aerialway:j-bar",                             },
	{ 0, 0, "aerialway:platter",                           },
	{ 0, 0, "aerialway:rope_tow",                          },
	{ 0, 0, "aerialway:magic_carpet",                      },
	{ 0, 0, "aerialway:zip_line",                          },
	{ 0, 0, "aerialway:pylon",                             },
	{ 0, 0, "aerialway:station",                           },
	{ 0, 0, "aerialway:canopy",                            },   // obsolete - convert to zipline
	{ 2, 0, "aeroway:aerodrome",                           },
	{ 0, 0, "aeroway:apron",                               },
	{ 0, 0, "aeroway:gate",                                },
	{ 0, 0, "aeroway:hangar",                              },
	{ 2, 0, "aeroway:helipad",                             },
	{ 2, 0, "aeroway:heliport",                            },
	{ 0, 0, "aeroway:navigationalaid",                     },
	{ 0, 0, "aeroway:runway",                              },
	{ 2, 0, "aeroway:spaceport",                           },
	{ 0, 0, "aeroway:taxilane",                            },
	{ 0, 0, "aeroway:taxiway",                             },
	{ 0, 0, "aeroway:terminal",                            },
	{ 0, 0, "aeroway:windsock",                            },
	{ 0, 0, "amenity:bar",                                 },
	{ 0, 0, "amenity:bbq",                                 },
	{ 0, 0, "amenity:biergarten",                          },
	{ 0, 0, "amenity:cafe",                                },
	{ 0, 0, "amenity:drinking_water",                      },
	{ 0, 0, "amenity:fast_food",                           },
	{ 0, 0, "amenity:food_court",                          },
	{ 0, 0, "amenity:ice_cream",                           },
	{ 0, 0, "amenity:pub",                                 },
	{ 0, 0, "amenity:restaurant",                          },
	{ 2, 0, "amenity:college"                              },
	{ 0, 0, "amenity:kindergarten",                        },
	{ 2, 0, "amenity:library",                             },
	{ 0, 0, "amenity:archive",                             },
	{ 0, 0, "amenity:public_bookcase",                     },
	{ 0, 0, "amenity:school",                              },
	{ 0, 0, "amenity:music_school",                        },
	{ 0, 0, "amenity:driving_school",                      },
	{ 0, 0, "amenity:language_school",                     },
	{ 2, 0, "amenity:university",                          },
	{ 0, 0, "amenity:research_institute",                  },
	{ 0, 0, "amenity:bicycle_parking",                     },
	{ 0, 0, "amenity:bicycle_repair_station",              },
	{ 0, 0, "amenity:bicycle_rental",                      },
	{ 0, 0, "amenity:boat_rental",                         },
	{ 0, 0, "amenity:boat_sharing",                        },
	{ 0, 0, "amenity:buggy_parking",                       },
	{ 0, 0, "amenity:bus_station",                         },
	{ 0, 0, "amenity:car_rental",                          },
	{ 0, 0, "amenity:car_sharing",                         },
	{ 0, 0, "amenity:car_wash",                            },
	{ 0, 0, "amenity:charging_station",                    },
	{ 0, 0, "amenity:ferry_terminal",                      },
	{ 0, 0, "amenity:fuel",                                },
	{ 0, 0, "amenity:grit_bin",                            },
	{ 0, 0, "amenity:motorcycle_parking",                  },
	{ 0, 0, "amenity:parking",                             },
	{ 0, 0, "amenity:parking_entrance",                    },
	{ 0, 0, "amenity:parking_space",                       },
	{ 0, 0, "amenity:taxi",                                },
	{ 0, 0, "amenity:ticket_validator",                    },
	{ 0, 0, "amenity:atm",                                 },
	{ 0, 0, "amenity:bank",                                },
	{ 0, 0, "amenity:bureau_de_change",                    },
	{ 0, 0, "amenity:baby_hatch",                          },
	{ 0, 0, "amenity:clinic",                              },
	{ 0, 0, "amenity:dentist",                             },
	{ 0, 0, "amenity:doctors",                             },
	{ 2, 0, "amenity:hospital",                            },
	{ 0, 0, "amenity:nursing_home",                        },
	{ 0, 0, "amenity:pharmacy",                            },
	{ 0, 0, "amenity:social_facility",                     },
	{ 0, 0, "amenity:veterinary",                          },
	{ 0, 0, "healthcare:blood_donation",                   },
	{ 0, 0, "amenity:arts_centre",                         },
	{ 0, 0, "amenity:brothel",                             },
	{ 0, 0, "amenity:casino",                              },
	{ 0, 0, "amenity:cinema",                              },
	{ 0, 0, "amenity:community_centre",                    },
	{ 0, 0, "amenity:fountain",                            },
	{ 0, 0, "amenity:gambling",                            },
	{ 0, 0, "amenity:nightclub",                           },
	{ 0, 0, "amenity:planetarium",                         },
	{ 0, 0, "amenity:social_centre",                       },
	{ 0, 0, "amenity:stripclub",                           },
	{ 0, 0, "amenity:studio",                              },
	{ 0, 0, "amenity:swingerclub",                         },
	{ 0, 0, "amenity:theatre",                             },
	{ 0, 0, "amenity:animal_boarding",                     },
	{ 0, 0, "amenity:animal_shelter",                      },
	{ 0, 0, "amenity:baking_oven",                         },
	{ 0, 0, "amenity:bench",                               },
	{ 0, 0, "amenity:clock",                               },
	{ 0, 0, "amenity:courthouse",                          },
	{ 0, 0, "amenity:coworking_space",                     },
	{ 0, 0, "amenity:crematorium",                         },
	{ 0, 0, "amenity:crypt",                               },
	{ 0, 0, "amenity:dive_centre",                         },
	{ 0, 0, "amenity:dojo",                                },
	{ 0, 0, "amenity:embassy",                             },
	{ 2, 0, "amenity:fire_station",                        },
	{ 0, 0, "amenity:game_feeding",                        },
	{ 0, 0, "amenity:grave_yard",                          },
	{ 0, 0, "amenity:hunting_stand",                       },
	{ 0, 0, "amenity:internet_cafe",                       },
	{ 0, 0, "amenity:kitchen",                             },
	{ 0, 0, "amenity:kneipp_water_cure",                   },
	{ 0, 0, "amenity:marketplace",                         },
	{ 0, 0, "amenity:photo_booth",                         },
	{ 0, 0, "amenity:place_of_warship",                    },
	{ 2, 0, "amenity:police",                              },
	{ 0, 0, "amenity:post_box",                            },
	{ 0, 0, "amenity:post_office",                         },
	{ 0, 0, "amenity:prison",                              },
	{ 0, 0, "amenity:public_bath",                         },
	{ 0, 0, "amenity:ranger_station",                      },
	{ 0, 0, "amenity:recycling",                           },
	{ 0, 0, "amenity:rescue_station",                      },
	{ 0, 0, "amenity:sanitary_dump_station",               },
	{ 0, 0, "amenity:shelter",                             },
	{ 0, 0, "amenity:shower",                              },
	{ 0, 0, "amenity:table",                               },
	{ 0, 0, "amenity:telephone",                           },
	{ 0, 0, "amenity:toilets",                             },
	{ 0, 0, "amenity:townhall",                            },
	{ 0, 0, "amenity:vending_machine",                     },
	{ 0, 0, "amenity:waste_basket",                        },
	{ 0, 0, "amenity:waste_disposal",                      },
	{ 0, 0, "amenity:waste_transfer_station",              },
	{ 0, 0, "amenity:watering_place",                      },
	{ 0, 0, "amenity:water_point",                         },
	{ 0, 0, "barrier:cable_barrier",                       },
	{ 0, 0, "barrier:city_wall",                           },
	{ 0, 0, "barrier:ditch",                               },
	{ 0, 0, "barrier:fence",                               },
	{ 0, 0, "barrier:guard_rail",                          },
	{ 0, 0, "barrier:handrail",                            },
	{ 0, 0, "barrier:hedge",                               },
	{ 0, 0, "barrier:kerb",                                },
	{ 0, 0, "barrier:retaining_wall",                      },
	{ 0, 0, "barrier:tank_trap",                           },
	{ 0, 0, "barrier:wall",                                },
	{ 0, 0, "barrier:block",                               },
	{ 0, 0, "barrier:bollard",                             },
	{ 0, 0, "barrier:border_control",                      },
	{ 0, 0, "barrier:bump_gate",                           },
	{ 0, 0, "barrier:bus_trap",                            },
	{ 0, 0, "barrier:cattle_grid",                         },
	{ 0, 0, "barrier:chain",                               },
	{ 0, 0, "barrier:cycle_barrier",                       },
	{ 0, 0, "barrier:debris",                              },
	{ 0, 0, "barrier:entrance",                            },
	{ 0, 0, "barrier:full-height_turnstyle",               },
	{ 0, 0, "barrier:gate",                                },
	{ 0, 0, "barrier:hampshire_gate",                      },
	{ 0, 0, "barrier:height_restrictor",                   },
	{ 0, 0, "barrier:horse_stile",                         },
	{ 0, 0, "barrier:jersey_barrier",                      },
	{ 0, 0, "barrier:kent_carriage_gap",                   },
	{ 0, 0, "barrier:kissing_gate",                        },
	{ 0, 0, "barrier:lift_gate",                           },
	{ 0, 0, "barrier:log",                                 },
	{ 0, 0, "barrier:motorcycle_barrier",                  },
	{ 0, 0, "barrier:rope",                                },
	{ 0, 0, "barrier:sally_port",                          },
	{ 0, 0, "barrier:spikes",                              },
	{ 0, 0, "barrier:stile",                               },
	{ 0, 0, "barrier:sump_buster",                         },
	{ 0, 0, "barrier:swing_gate",                          },
	{ 0, 0, "barrier:toll_booth",                          },
	{ 0, 0, "barrier:turnstile",                           },
	{ 0, 0, "barrier:yes",                                 },
	{ 0, 0, "boundary:administrative",                     },
	{ 0, 0, "boundary:historic",                           },
	{ 0, 0, "boundary:maritime",                           },
	{ 3, 0, "boundary:national_park",                      },
	{ 0, 0, "boundary:political",                          },
	{ 0, 0, "boundary:postal_code",                        },
	{ 0, 0, "boundary:religious_administration",           },
	{ 0, 0, "boundary:protected_area",                     },
	{ 0, 1, "building:apartments",                         },
	{ 0, 1, "building:farm",                               },
	{ 0, 1, "building:hotel",                              },
	{ 0, 1, "building:house",                              },
	{ 0, 1, "building:detached",                           },
	{ 0, 1, "building:residential",                        },
	{ 0, 1, "building:dormatory",                          },
	{ 0, 1, "building:terrace",                            },
	{ 0, 1, "building:houseboat",                          },
	{ 0, 1, "building:bungalow",                           },
	{ 0, 1, "building:static_caravan",                     },
	{ 0, 1, "building:cabin",                              },
	{ 0, 1, "building:commercial",                         },
	{ 0, 1, "building:office",                             },
	{ 0, 1, "building:industrial",                         },
	{ 0, 1, "building:retail",                             },
	{ 0, 1, "building:warehouse",                          },
	{ 0, 1, "building:kiosk",                              },
	{ 0, 1, "building:religious",                          },
	{ 0, 1, "building:cathedral",                          },
	{ 0, 1, "building:chapel",                             },
	{ 0, 1, "building:church",                             },
	{ 0, 1, "building:mosque",                             },
	{ 0, 1, "building:temple",                             },
	{ 0, 1, "building:synagogue",                          },
	{ 0, 1, "building:shrine",                             },
	{ 0, 1, "building:bakehouse",                          },
	{ 0, 1, "building:kindergarten",                       },
	{ 0, 1, "building:civic",                              },
	{ 0, 1, "building:hospital",                           },
	{ 0, 1, "building:school",                             },
	{ 0, 1, "building:stadium",                            },
	{ 0, 1, "building:train_station",                      },
	{ 0, 1, "building:transportation",                     },
	{ 0, 1, "building:university",                         },
	{ 0, 1, "building:grandstand",                         },
	{ 0, 1, "building:public",                             },
	{ 0, 1, "building:barn",                               },
	{ 0, 1, "building:bridge",                             },
	{ 0, 1, "building:bunker",                             },
	{ 0, 1, "building:carport",                            },
	{ 0, 1, "building:conservatory",                       },
	{ 0, 1, "building:construction",                       },
	{ 0, 1, "building:cowshed",                            },
	{ 0, 1, "building:digester",                           },
	{ 0, 1, "building:farm_auxilary",                      },
	{ 0, 1, "building:garage",                             },
	{ 0, 1, "building:garages",                            },
	{ 0, 1, "building:garbage_shed",                       },
	{ 0, 1, "building:greenhouse",                         },
	{ 0, 1, "building:hangar",                             },
	{ 0, 1, "building:hut",                                },
	{ 0, 1, "building:pavilion",                           },
	{ 0, 1, "building:parking",                            },
	{ 0, 1, "building:riding_hall",                        },
	{ 0, 1, "building:roof",                               },
	{ 0, 1, "building:shed",                               },
	{ 0, 1, "building:sports_hall",                        },
	{ 0, 1, "building:stable",                             },
	{ 0, 1, "building:sty",                                },
	{ 0, 1, "building:transformer_tower",                  },
	{ 0, 1, "building:service",                            },
	{ 0, 1, "building:ruins",                              },
	{ 0, 1, "building:water_tower",                        },
	{ 0, 1, "building:yes",                                },
	{ 0, 0, "craft:agricultural_engines",                  },
	{ 0, 0, "craft:bakery",                                },
	{ 0, 0, "craft:basket_maker",                          },
	{ 0, 0, "craft:beekeeper",                             },
	{ 0, 0, "craft:blacksmith",                            },
	{ 0, 0, "craft:boatbuilder",                           },
	{ 0, 0, "craft:bookbinder",                            },
	{ 0, 0, "craft:brewery",                               },
	{ 0, 0, "craft:builder",                               },
	{ 0, 0, "craft:carpenter",                             },
	{ 0, 0, "craft:carpet_layer",                          },
	{ 0, 0, "craft:caterer",                               },
	{ 0, 0, "craft:chimney_sweeper",                       },
	{ 0, 0, "craft:clockmaker",                            },
	{ 0, 0, "craft:confectionery",                         },
	{ 0, 0, "craft:dental_technican",                      },
	{ 0, 0, "craft:distillery",                            },
	{ 0, 0, "craft:dressmaker",                            },
	{ 0, 0, "craft:embroiderer",                           },
	{ 0, 0, "craft:electrician",                           },
	{ 0, 0, "craft:engraver",                              },
	{ 0, 0, "craft:floorer",                               },
	{ 0, 0, "craft:gardener",                              },
	{ 0, 0, "craft:glaziery",                              },
	{ 0, 0, "craft:handicraft",                            },
	{ 0, 0, "craft:hvac",                                  },
	{ 0, 0, "craft:insulation",                            },
	{ 0, 0, "craft:jeweller",                              },
	{ 0, 0, "craft:joiner",                                },
	{ 0, 0, "craft:key_cutter",                            },
	{ 0, 0, "craft:locksmith",                             },
	{ 0, 0, "craft:metal_construction",                    },
	{ 0, 0, "craft:mint",                                  },
	{ 0, 0, "craft:optician",                              },
	{ 0, 0, "craft:painter",                               },
	{ 0, 0, "craft:photographer",                          },
	{ 0, 0, "craft:photographic_laboratory",               },
	{ 0, 0, "craft:piano_tuner",                           },
	{ 0, 0, "craft:plasterer",                             },
	{ 0, 0, "craft:plumber",                               },
	{ 0, 0, "craft:pottery",                               },
	{ 0, 0, "craft:printmaker",                            },
	{ 0, 0, "craft:rigger",                                },
	{ 0, 0, "craft:roofer",                                },
	{ 0, 0, "craft:saddler",                               },
	{ 0, 0, "craft:sailmaker",                             },
	{ 0, 0, "craft:sawmill",                               },
	{ 0, 0, "craft:scaffolder",                            },
	{ 0, 0, "craft:sculpter",                              },
	{ 0, 0, "craft:shoemaker",                             },
	{ 0, 0, "craft:stand_builder",                         },
	{ 0, 0, "craft:stonemason",                            },
	{ 0, 0, "craft:sun_protection",                        },
	{ 0, 0, "craft:tailor",                                },
	{ 0, 0, "craft:tiler",                                 },
	{ 0, 0, "craft:tinsmith",                              },
	{ 0, 0, "craft:toolmaker",                             },
	{ 0, 0, "craft:turner",                                },
	{ 0, 0, "craft:upholsterer",                           },
	{ 0, 0, "craft:watchmaker",                            },
	{ 0, 0, "craft:window_construction",                   },
	{ 0, 0, "craft:winery",                                },
	{ 0, 0, "emergency:ambulance_station",                 },
	{ 0, 0, "emergency:defibrillator",                     },
	{ 0, 0, "emergency:first_aid_kit",                     },
	{ 0, 0, "emergency:landing_site",                      },
	{ 0, 0, "emergency:emergency_ward_entrance",           },
	{ 0, 0, "emergency:dry_riser_inlet",                   },
	{ 0, 0, "emergency:fire_alarm_box",                    },
	{ 0, 0, "emergency:fire_extinguisher",                 },
	{ 0, 0, "emergency:fire_flapper",                      },
	{ 0, 0, "emergency:fire_hose",                         },
	{ 0, 0, "emergency:fire_hydrant",                      },
	{ 0, 0, "emergency:water_tank",                        },
	{ 0, 0, "emergency:fire_water_pond",                   },
	{ 0, 0, "emergency:suction_point",                     },
	{ 0, 0, "emergency:lifeguard",                         },
	{ 0, 0, "emergency:lifeguard_base",                    },
	{ 0, 0, "emergency:lifeguard_tower",                   },
	{ 0, 0, "emergency:lifeguard_platform",                },
	{ 0, 0, "emergency:life_ring",                         },
	{ 0, 0, "emergency:mountain_rescue",                   },
	{ 0, 0, "emergency:ses_station",                       },
	{ 0, 0, "emergency:assembly_point",                    },
	{ 0, 0, "emergency:access_point",                      },
	{ 0, 0, "emergency:phone",                             },
	{ 0, 0, "emergency:rescue_box",                        },
	{ 0, 0, "emergency:siren",                             },
	{ 0, 0, "geological:moraine",                          },
	{ 0, 0, "geological:outcrop",                          },
	{ 0, 0, "geological:palaeontological_site",            },
	{ 0, 0, "highway:motorway",                            },
	{ 0, 0, "highway:trunk",                               },
	{ 0, 0, "highway:primary",                             },
	{ 0, 0, "highway:secondary",                           },
	{ 0, 0, "highway:tertiary",                            },
	{ 0, 0, "highway:unclassified",                        },
	{ 0, 0, "highway:residential",                         },
	{ 0, 0, "highway:service",                             },
	{ 0, 0, "highway:motorway_link",                       },
	{ 0, 0, "highway:trunk_link",                          },
	{ 0, 0, "highway:primary_link",                        },
	{ 0, 0, "highway:secondary_link",                      },
	{ 0, 0, "highway:tertiary_link",                       },
	{ 1, 0, "highway:living_street",                       },
	{ 1, 0, "highway:pedestrian",                          },
	{ 1, 0, "highway:track",                               },
	{ 0, 0, "highway:bus_guideway",                        },
	{ 0, 0, "highway:escape",                              },
	{ 0, 0, "highway:raceway",                             },
	{ 0, 0, "highway:road",                                },
	{ 1, 0, "highway:footway",                             },
	{ 1, 0, "highway:bridleway",                           },
	{ 1, 0, "highway:steps",                               },
	{ 1, 0, "highway:path",                                },
	{ 1, 0, "highway:cycleway",                            },
	{ 0, 0, "highway:bus_stop",                            },
	{ 0, 0, "highway:crossing",                            },
	{ 0, 0, "highway:elevator",                            },
	{ 0, 0, "highway:emergency_access_point",              },
	{ 0, 0, "highway:give_way",                            },
	{ 0, 0, "highway:mini_roundabout",                     },
	{ 0, 0, "highway:motorway_junction",                   },
	{ 0, 0, "highway:passing_place",                       },
	{ 0, 0, "highway:rest_area",                           },
	{ 0, 0, "highway:speed_camera",                        },
	{ 0, 0, "highway:street_lamp",                         },
	{ 0, 0, "highway:services",                            },
	{ 0, 0, "highway:stop",                                },
	{ 0, 0, "highway:traffic_signals",                     },
	{ 0, 0, "highway:turning_circle",                      },
	{ 0, 0, "historic:aircraft",                           },
	{ 0, 0, "historic:aqueduct",                           },
	{ 0, 0, "historic:archaeological_site",                },
	{ 0, 0, "historic:battlefield",                        },
	{ 0, 0, "historic:boundary_stone",                     },
	{ 0, 1, "historic:building",                           },
	{ 0, 0, "historic:cannon",                             },
	{ 0, 0, "historic:castle",                             },
	{ 0, 0, "historic:castle_wall",                        },
	{ 0, 0, "historic:church",                             },
	{ 0, 0, "historic:city_gate",                          },
	{ 0, 0, "historic:citywalls",                          },
	{ 0, 0, "historic:farm",                               },
	{ 0, 0, "historic:fort",                               },
	{ 0, 0, "historic:gallows",                            },
	{ 0, 0, "historic:highwater_mark",                     },
	{ 0, 0, "historic:locomotive",                         },
	{ 0, 0, "historic:manor",                              },
	{ 0, 0, "historic:memorial",                           },
	{ 0, 0, "historic:milestone",                          },
	{ 0, 0, "historic:monastery",                          },
	{ 0, 0, "historic:monument",                           },
	{ 0, 0, "historic:optical_telegraph",                  },
	{ 0, 0, "historic:pillory",                            },
	{ 0, 0, "historic:railway_car",                        },
	{ 0, 0, "historic:ruins",                              },
	{ 0, 0, "historic:rune_stone",                         },
	{ 0, 0, "historic:ship",                               },
	{ 0, 0, "historic:tomb",                               },
	{ 0, 0, "historic:tower",                              },
	{ 0, 0, "historic:wayside_cross",                      },
	{ 0, 0, "historic:wayside_shrine",                     },
	{ 0, 0, "historic:wreck",                              },
	{ 0, 0, "historic:yes",                                },
	{ 0, 0, "landuse:commercial",                          },
	{ 0, 0, "landuse:construction",                        },
	{ 0, 0, "landuse:industrial",                          },
	{ 0, 0, "landuse:residential",                         },
	{ 0, 0, "landuse:retail",                              },
	{ 0, 0, "landuse:allotments",                          },
	{ 3, 0, "landuse:basin",                               },
	{ 0, 0, "landuse:brownfield",                          },
	{ 0, 0, "landuse:cemetery",                            },
	{ 0, 0, "landuse:depot",                               },
	{ 0, 0, "landuse:farmland",                            },
	{ 0, 0, "landuse:farmyard",                            },
	{ 3, 0, "landuse:forest",                              },
	{ 0, 0, "landuse:garages",                             },
	{ 0, 0, "landuse:grass",                               },
	{ 0, 0, "landuse:greenfield",                          },
	{ 0, 0, "landuse:greenhouse_horticulture",             },
	{ 0, 0, "landuse:landfill",                            },
	{ 0, 0, "landuse:meadow",                              },
	{ 0, 0, "landuse:military",                            },
	{ 0, 0, "landuse:orchard",                             },
	{ 0, 0, "landuse:plant_nursery",                       },
	{ 0, 0, "landuse:port",                                },
	{ 0, 0, "landuse:quarry",                              },
	{ 0, 0, "landuse:railway",                             },
	{ 0, 0, "landuse:recreation_ground",                   },
	{ 0, 0, "landuse:winter_sports",                       },
	{ 0, 0, "landuse:religious",                           },
	{ 3, 0, "landuse:reservoir",                           },
	{ 3, 0, "landuse:salt_pond",                           },
	{ 0, 0, "landuse:village_green",                       },
	{ 0, 0, "landuse:vineyard",                            },
	{ 0, 0, "leisure:adult_gaming_centre",                 },
	{ 0, 0, "leisure:amusement_arcade",                    },
	{ 0, 0, "leisure:beach_resort",                        },
	{ 0, 0, "leisure:bandstand",                           },
	{ 0, 0, "leisure:bird_hide",                           },
	{ 0, 0, "leisure:common",                              },
	{ 0, 0, "leisure:dance",                               },
	{ 1, 0, "leisure:disc_golf_course",                    },
	{ 1, 0, "leisure:dog_park",                            },
	{ 0, 0, "leisure:escape_game",                         },
	{ 0, 0, "leisure:firepit",                             },
	{ 0, 0, "leisure:fishing",                             },
	{ 0, 0, "leisure:fitness_centre",                      },
	{ 0, 0, "leisure:fitness_station",                     },
	{ 0, 0, "leisure:garden",                              },
	{ 0, 0, "leisure:hackerspace",                         },
	{ 0, 0, "leisure:horse_riding",                        },
	{ 0, 0, "leisure:ice_rink",                            },
	{ 0, 0, "leisure:marina",                              },
	{ 0, 0, "leisure:minature_golf",                       },
	{ 2, 0, "leisure:nature_reserve",                      },
	{ 2, 0, "leisure:park",                                },
	{ 0, 0, "leisure:picnic_table",                        },
	{ 0, 0, "leisure:pitch",                               },
	{ 0, 0, "leisure:playground",                          },
	{ 0, 0, "leisure:slipway",                             },
	{ 0, 0, "leisure:sports_centre",                       },
	{ 0, 0, "leisure:stadium",                             },
	{ 0, 0, "leisure:summer_camp",                         },
	{ 0, 0, "leisure:swimming_area",                       },
	{ 0, 0, "leisure:swimming_pool",                       },
	{ 0, 0, "leisure:track",                               },
	{ 0, 0, "leisure:water_park",                          },
	{ 0, 0, "leisure:wildlife_hide",                       },
	{ 0, 0, "man_made:adit",                               },
	{ 0, 0, "man_made:beacon",                             },
	{ 0, 0, "man_made:breakwater",                         },
	{ 2, 0, "man_made:bridge",                             },
	{ 0, 0, "man_made:bunker_silo",                        },
	{ 0, 0, "man_made:campanile",                          },
	{ 0, 0, "man_made:chimney",                            },
	{ 0, 0, "man_made:communications_tower",               },
	{ 0, 0, "man_made:crane",                              },
	{ 0, 0, "man_made:cross",                              },
	{ 0, 0, "man_made:cutline",                            },
	{ 0, 0, "man_made:clearcut",                           },
	{ 0, 0, "man_made:dovecote",                           },
	{ 0, 0, "man_made:drinking_fountain",                  },
	{ 0, 0, "man_made:dyke",                               },
	{ 0, 0, "man_made:embankment",                         },
	{ 0, 0, "man_made:flagpole",                           },
	{ 0, 0, "man_made:gasometer",                          },
	{ 0, 0, "man_made:groyne",                             },
	{ 0, 0, "man_made:guy",                                },
	{ 0, 0, "man_made:kiln",                               },
	{ 0, 0, "man_made:lighthouse",                         },
	{ 0, 0, "man_made:mast",                               },
	{ 0, 0, "man_made:mineshaft",                          },
	{ 0, 0, "man_made:monitoring_station",                 },
	{ 0, 0, "man_made:obelisk",                            },
	{ 0, 0, "man_made:observatory",                        },
	{ 0, 0, "man_made:offshore_platform",                  },
	{ 0, 0, "man_made:petroleum_well",                     },
	{ 0, 0, "man_made:pier",                               },
	{ 0, 0, "man_made:pipeline",                           },
	{ 0, 0, "man_made:pumping_station",                    },
	{ 0, 0, "man_made:reservoir_covered",                  },
	{ 0, 0, "man_made:silo",                               },
	{ 0, 0, "man_made:snow_fence",                         },
	{ 0, 0, "man_made:snow_net",                           },
	{ 0, 0, "man_made:storage_tank",                       },
	{ 0, 0, "man_made:street_cabinet",                     },
	{ 0, 0, "man_made:surveillance",                       },
	{ 0, 0, "man_made:survey_point",                       },
	{ 0, 0, "man_made:telescope",                          },
	{ 0, 0, "man_made:tower",                              },
	{ 0, 0, "man_made:wastewater_plant",                   },
	{ 0, 0, "man_made:watermill",                          },
	{ 0, 0, "man_made:water_tower",                        },
	{ 0, 0, "man_made:water_well",                         },
	{ 0, 0, "man_made:water_tap",                          },
	{ 0, 0, "man_made:water_works",                        },
	{ 0, 0, "man_made:wildlife_crossing",                  },
	{ 0, 0, "man_made:windmill",                           },
	{ 0, 0, "man_made:works",                              },
	{ 0, 0, "man_made:yes",                                },
	{ 0, 0, "military:airfield",                           },
	{ 0, 0, "military:ammunition",                         },
	{ 0, 0, "military:bunker",                             },
	{ 0, 0, "military:barracks",                           },
	{ 0, 0, "military:checkpoint",                         },
	{ 0, 0, "military:danger_area",                        },
	{ 0, 0, "military:naval_base",                         },
	{ 0, 0, "military:nuclear_explosion_site",             },
	{ 0, 0, "military:obstacle_course",                    },
	{ 0, 0, "military:office",                             },
	{ 0, 0, "military:range",                              },
	{ 0, 0, "military:training_area",                      },
	{ 0, 0, "military:trench",                             },
	{ 0, 0, "military:launchpad",                          },
	{ 2, 0, "natural:wood",                                },
	{ 2, 0, "natural:tree_row",                            },
	{ 2, 0, "natural:tree",                                },
	{ 2, 0, "natural:scrub",                               },
	{ 2, 0, "natural:heath",                               },
	{ 2, 0, "natural:moor",                                },
	{ 2, 0, "natural:grassland",                           },
	{ 2, 0, "natural:fell",                                },
	{ 2, 0, "natural:bare_rock",                           },
	{ 2, 0, "natural:scree",                               },
	{ 2, 0, "natural:shingle",                             },
	{ 2, 0, "natural:sand",                                },
	{ 2, 0, "natural:mud",                                 },
	{ 3, 0, "natural:water",                               },
	{ 2, 0, "natural:wetland",                             },
	{ 3, 0, "natural:glacier",                             },
	{ 3, 0, "natural:bay",                                 },
	{ 2, 0, "natural:cape",                                },
	{ 2, 0, "natural:beach",                               },
	{ 2, 0, "natural:coastline",                           },
	{ 2, 0, "natural:spring",                              },
	{ 2, 0, "natural:hot_spring",                          },
	{ 2, 0, "natural:geyser",                              },
	{ 2, 0, "natural:blowhole",                            },
	{ 3, 0, "natural:peak",                                },
	{ 3, 0, "natural:volcano",                             },
	{ 3, 0, "natural:valley",                              },
	{ 2, 0, "natural:ridge",                               },
	{ 2, 0, "natural:arete",                               },
	{ 2, 0, "natural:cliff",                               },
	{ 3, 0, "natural:saddle",                              },
	{ 2, 0, "natural:rock",                                },
	{ 2, 0, "natural:stone",                               },
	{ 2, 0, "natural:sinkhole",                            },
	{ 2, 0, "natural:cave_entrance",                       },
	{ 0, 0, "office:accountant",                           },
	{ 0, 0, "office:adoption_agency",                      },
	{ 0, 0, "office:advertising_agency",                   },
	{ 0, 0, "office:architect",                            },
	{ 0, 0, "office:association",                          },
	{ 0, 0, "office:charity",                              },
	{ 0, 0, "office:company",                              },
	{ 0, 0, "office:educational_institution",              },
	{ 0, 0, "office:employment_agency",                    },
	{ 0, 0, "office:energy_supplier",                      },
	{ 0, 0, "office:estate_agent",                         },
	{ 0, 0, "office:forestry",                             },
	{ 0, 0, "office:foundation",                           },
	{ 0, 0, "office:government",                           },
	{ 0, 0, "office:guide",                                },
	{ 0, 0, "office:healer",                               },
	{ 0, 0, "office:insurance",                            },
	{ 0, 0, "office:it",                                   },
	{ 0, 0, "office:lawyer",                               },
	{ 0, 0, "office:logistics",                            },
	{ 0, 0, "office:moving_company",                       },
	{ 0, 0, "office:newspaper",                            },
	{ 0, 0, "office:ngo",                                  },
	{ 0, 0, "office:notary",                               },
	{ 0, 0, "office:physican",                             },
	{ 0, 0, "office:political_party",                      },
	{ 0, 0, "office:private_investigator",                 },
	{ 0, 0, "office:property_management",                  },
	{ 0, 0, "office:quango",                               },
	{ 0, 0, "office:real_estate_agent",                    },
	{ 0, 0, "office:religion",                             },
	{ 0, 0, "office:research",                             },
	{ 0, 0, "office:surveyor",                             },
	{ 0, 0, "office:tax",                                  },
	{ 0, 0, "office:tax_advisor",                          },
	{ 0, 0, "office:telecommunication",                    },
	{ 0, 0, "office:therapist",                            },
	{ 0, 0, "office:travel_agent",                         },
	{ 0, 0, "office:water_utility",                        },
	{ 0, 0, "office:yes",                                  },
	{ 5, 0, "place:country",                               },
	{ 5, 0, "place:state",                                 },
	{ 2, 0, "place:region",                                },
	{ 2, 0, "place:province",                              },
	{ 2, 0, "place:district",                              },
	{ 2, 0, "place:county",                                },
	{ 2, 0, "place:municipality",                          },
	{ 4, 0, "place:city",                                  },
	{ 2, 0, "place:borough",                               },
	{ 2, 0, "place:suburb",                                },
	{ 2, 0, "place:quarter",                               },
	{ 0, 0, "place:neighbourhood",                         },
	{ 0, 0, "place:city_block",                            },
	{ 0, 0, "place:plot",                                  },
	{ 4, 0, "place:town",                                  },
	{ 2, 0, "place:village",                               },
	{ 2, 0, "place:hamlet",                                },
	{ 0, 0, "place:isolated_dwelling",                     },
	{ 0, 0, "place:farm",                                  },
	{ 0, 0, "place:allotments",                            },
	{ 5, 0, "place:continent",                             },
	{ 4, 0, "place:archipelago",                           },
	{ 4, 0, "place:island",                                },
	{ 2, 0, "place:islet",                                 },
	{ 0, 0, "place:square",                                },
	{ 2, 0, "place:locality",                              },
	{ 0, 0, "power:plant",                                 },
	{ 0, 0, "power:cable",                                 },
	{ 0, 0, "power:compensator",                           },
	{ 0, 0, "power:convertor",                             },
	{ 0, 0, "power:generator",                             },
	{ 0, 0, "power:heliostat",                             },
	{ 0, 0, "power:insulator",                             },
	{ 0, 0, "power:line",                                  },
	{ 0, 0, "line:busbar",                                 },
	{ 0, 0, "line:bay",                                    },
	{ 0, 0, "power:minor_line",                            },
	{ 0, 0, "power:pole",                                  },
	{ 0, 0, "power:portal",                                },
	{ 0, 0, "power:catenary_mast",                         },
	{ 0, 0, "power:substation",                            },
	{ 0, 0, "power:switch",                                },
	{ 0, 0, "power:terminal",                              },
	{ 0, 0, "power:tower",                                 },
	{ 0, 0, "power:transformer",                           },
	{ 0, 0, "public_transport:stop_position",              },
	{ 0, 0, "public_transport:platform",                   },
	{ 0, 0, "public_transport:station",                    },
	{ 0, 0, "public_transport:stop_area",                  },
	{ 0, 0, "railway:abandoned",                           },
	{ 0, 0, "railway:construction",                        },
	{ 0, 0, "railway:disused",                             },
	{ 1, 0, "railway:funicular",                           },
	{ 1, 0, "railway:light_rail",                          },
	{ 0, 0, "railway:minature",                            },
	{ 1, 0, "railway:monorail",                            },
	{ 1, 0, "railway:narrow_gauge",                        },
	{ 1, 0, "railway:preserved",                           },
	{ 1, 0, "railway:rail",                                },
	{ 1, 0, "railway:subway",                              },
	{ 1, 0, "railway:tram",                                },
	{ 0, 0, "railway:halt",                                },
	{ 0, 0, "railway:platform",                            },
	{ 1, 0, "railway:station",                             },
	{ 0, 0, "railway:subway_entrance",                     },
	{ 0, 0, "railway:tram_stop",                           },
	{ 0, 0, "railway:buffer_stop",                         },
	{ 0, 0, "railway:derail",                              },
	{ 0, 0, "railway:crossing",                            },
	{ 0, 0, "railway:level_crossing",                      },
	{ 0, 0, "railway:signal",                              },
	{ 0, 0, "railway:switch",                              },
	{ 0, 0, "railway:railway_crossing",                    },
	{ 0, 0, "railway:turntable",                           },
	{ 0, 0, "railway:roundhouse",                          },
	{ 0, 0, "railway:traverser",                           },
	{ 0, 0, "railway:wash",                                },
	{ 0, 0, "shop:alcohol",                                },
	{ 0, 0, "shop:bakery",                                 },
	{ 0, 0, "shop:beverages",                              },
	{ 0, 0, "shop:brewing_supplies",                       },
	{ 0, 0, "shop:butcher",                                },
	{ 0, 0, "shop:cheese",                                 },
	{ 0, 0, "shop:chocolate",                              },
	{ 0, 0, "shop:coffee",                                 },
	{ 0, 0, "shop:confectionery",                          },
	{ 0, 0, "shop:convenience",                            },
	{ 0, 0, "shop:deli",                                   },
	{ 0, 0, "shop:dairy",                                  },
	{ 0, 0, "shop:farm",                                   },
	{ 0, 0, "shop:frozen_food",                            },
	{ 0, 0, "shop:greengrocier",                           },
	{ 0, 0, "shop:health_food",                            },
	{ 0, 0, "shop:ice_cream",                              },
	{ 0, 0, "shop:pasta",                                  },
	{ 0, 0, "shop:pastry",                                 },
	{ 0, 0, "shop:seafood",                                },
	{ 0, 0, "shop:spices",                                 },
	{ 0, 0, "shop:tea",                                    },
	{ 0, 0, "shop:water",                                  },
	{ 0, 0, "shop:department_store",                       },
	{ 0, 0, "shop:general",                                },
	{ 0, 0, "shop:kiosk",                                  },
	{ 0, 0, "shop:mall",                                   },
	{ 0, 0, "shop:supermarket",                            },
	{ 0, 0, "shop:wholesale",                              },
	{ 0, 0, "shop:baby_goods",                             },
	{ 0, 0, "shop:bag",                                    },
	{ 0, 0, "shop:boutique",                               },
	{ 0, 0, "shop:clothes",                                },
	{ 0, 0, "shop:fabric",                                 },
	{ 0, 0, "shop:fashion",                                },
	{ 0, 0, "shop:jewelry",                                },
	{ 0, 0, "shop:leather",                                },
	{ 0, 0, "shop:sewing",                                 },
	{ 0, 0, "shop:shoes",                                  },
	{ 0, 0, "shop:tailor",                                 },
	{ 0, 0, "shop:watches",                                },
	{ 0, 0, "shop:charity",                                },
	{ 0, 0, "shop:second_hand",                            },
	{ 0, 0, "shop:variety_store",                          },
	{ 0, 0, "shop:beauty",                                 },
	{ 0, 0, "shop:chemist",                                },
	{ 0, 0, "shop:cosmetics",                              },
	{ 0, 0, "shop:erotic",                                 },
	{ 0, 0, "shop:hairdresser",                            },
	{ 0, 0, "shop:hairdresser_suply",                      },
	{ 0, 0, "shop:hearing_aids",                           },
	{ 0, 0, "shop:herbalist",                              },
	{ 0, 0, "shop:massage",                                },
	{ 0, 0, "shop:medical_supply",                         },
	{ 0, 0, "shop:nutrition_supplements",                  },
	{ 0, 0, "shop:optician",                               },
	{ 0, 0, "shop:perfumery",                              },
	{ 0, 0, "shop:tattoo",                                 },
	{ 0, 0, "shop:agrarian",                               },
	{ 0, 0, "shop:appliance",                              },
	{ 0, 0, "shop:bathroom_furnishing",                    },
	{ 0, 0, "shop:doityourself",                           },
	{ 0, 0, "shop:electrical",                             },
	{ 0, 0, "shop:energy",                                 },
	{ 0, 0, "shop:fireplace",                              },
	{ 0, 0, "shop:florist",                                },
	{ 0, 0, "shop:garden_centre",                          },
	{ 0, 0, "shop:garden_furniture",                       },
	{ 0, 0, "shop:gas",                                    },
	{ 0, 0, "shop:glaziery",                               },
	{ 0, 0, "shop:hardware",                               },
	{ 0, 0, "shop:houseware",                              },
	{ 0, 0, "shop:locksmith",                              },
	{ 0, 0, "shop:paint",                                  },
	{ 0, 0, "shop:security",                               },
	{ 0, 0, "shop:trade",                                  },
	{ 0, 0, "shop:antiques",                               },
	{ 0, 0, "shop:bed",                                    },
	{ 0, 0, "shop:candles",                                },
	{ 0, 0, "shop:carpet",                                 },
	{ 0, 0, "shop:curtain",                                },
	{ 0, 0, "shop:doors",                                  },
	{ 0, 0, "shop:flooring",                               },
	{ 0, 0, "shop:furniture",                              },
	{ 0, 0, "shop:interior_decoration",                    },
	{ 0, 0, "shop:kitchen",                                },
	{ 0, 0, "shop:lamps",                                  },
	{ 0, 0, "shop:tiles",                                  },
	{ 0, 0, "shop:window_blind",                           },
	{ 0, 0, "shop:computer",                               },
	{ 0, 0, "shop:robot",                                  },
	{ 0, 0, "shop:electronics",                            },
	{ 0, 0, "shop:hifi",                                   },
	{ 0, 0, "shop:mobile_phone",                           },
	{ 0, 0, "shop:radiotechnics",                          },
	{ 0, 0, "shop:vacuum_cleaner",                         },
	{ 0, 0, "shop:atv",                                    },
	{ 0, 0, "shop:bicycle",                                },
	{ 0, 0, "shop:boat",                                   },
	{ 0, 0, "shop:car",                                    },
	{ 0, 0, "shop:car_repair",                             },
	{ 0, 0, "shop:car_parts",                              },
	{ 0, 0, "shop:fuel",                                   },
	{ 0, 0, "shop:fishing",                                },
	{ 0, 0, "shop:free_flying",                            },
	{ 0, 0, "shop:hunting",                                },
	{ 0, 0, "shop:jetski",                                 },
	{ 0, 0, "shop:motorcycle",                             },
	{ 0, 0, "shop:outdoor",                                },
	{ 0, 0, "shop:scuba_diving",                           },
	{ 0, 0, "shop:ski",                                    },
	{ 0, 0, "shop:snowmobile",                             },
	{ 0, 0, "shop:sports",                                 },
	{ 0, 0, "shop:swimming_pool",                          },
	{ 0, 0, "shop:tyres",                                  },
	{ 0, 0, "shop:art",                                    },
	{ 0, 0, "shop:collector",                              },
	{ 0, 0, "shop:craft",                                  },
	{ 0, 0, "shop:frame",                                  },
	{ 0, 0, "shop:games",                                  },
	{ 0, 0, "shop:model",                                  },
	{ 0, 0, "shop:music",                                  },
	{ 0, 0, "shop:musical_instrument",                     },
	{ 0, 0, "shop:photo",                                  },
	{ 0, 0, "shop:camera",                                 },
	{ 0, 0, "shop:trophy",                                 },
	{ 0, 0, "shop:video",                                  },
	{ 0, 0, "shop:video_games",                            },
	{ 0, 0, "shop:anime",                                  },
	{ 0, 0, "shop:books",                                  },
	{ 0, 0, "shop:gift",                                   },
	{ 0, 0, "shop:lottery",                                },
	{ 0, 0, "shop:newsagent",                              },
	{ 0, 0, "shop:stationery",                             },
	{ 0, 0, "shop:ticket",                                 },
	{ 0, 0, "shop:bookmaker",                              },
	{ 0, 0, "shop:copyshop",                               },
	{ 0, 0, "shop:dry_cleaning",                           },
	{ 0, 0, "shop:e-cigarette",                            },
	{ 0, 0, "shop:funeral_directors",                      },
	{ 0, 0, "shop:laundry",                                },
	{ 0, 0, "shop:money_lender",                           },
	{ 0, 0, "shop:party",                                  },
	{ 0, 0, "shop:pawnbroker",                             },
	{ 0, 0, "shop:pet",                                    },
	{ 0, 0, "shop:pyrotechnics",                           },
	{ 0, 0, "shop:religion",                               },
	{ 0, 0, "shop:storage_rental",                         },
	{ 0, 0, "shop:tobacco",                                },
	{ 0, 0, "shop:toys",                                   },
	{ 0, 0, "shop:travel_agency",                          },
	{ 0, 0, "shop:vacant",                                 },
	{ 0, 0, "shop:weapons",                                },
	{ 0, 0, "sport:9pin",                                  },
	{ 0, 0, "sport:10pin",                                 },
	{ 0, 0, "sport:american_football",                     },
	{ 0, 0, "sport:aikido",                                },
	{ 0, 0, "sport:archery",                               },
	{ 0, 0, "sport:athletics",                             },
	{ 0, 0, "sport:australian_football",                   },
	{ 0, 0, "sport:badminton",                             },
	{ 0, 0, "sport:bandy",                                 },
	{ 0, 0, "sport:base",                                  },
	{ 0, 0, "sport:baseball",                              },
	{ 0, 0, "sport:basketball",                            },
	{ 0, 0, "sport:beachvolleyball",                       },
	{ 0, 0, "sport:billards",                              },
	{ 0, 0, "sport:bmx",                                   },
	{ 0, 0, "sport:bobsleigh",                             },
	{ 0, 0, "sport:boules",                                },
	{ 0, 0, "sport:bowls",                                 },
	{ 0, 0, "sport:boxing",                                },
	{ 0, 0, "sport:canadian_football",                     },
	{ 0, 0, "sport:canoe",                                 },
	{ 0, 0, "sport:chess",                                 },
	{ 0, 0, "sport:cliff_diving",                          },
	{ 0, 0, "sport:climbing",                              },
	{ 0, 0, "sport:climbing_adventure",                    },
	{ 0, 0, "sport:cricket",                               },
	{ 0, 0, "sport:croquet",                               },
	{ 0, 0, "sport:curling",                               },
	{ 0, 0, "sport:cycling",                               },
	{ 0, 0, "sport:darts",                                 },
	{ 0, 0, "sport:dog_racing",                            },
	{ 0, 0, "sport:equestrian",                            },
	{ 0, 0, "sport:fencing",                               },
	{ 0, 0, "sport:field_hockey",                          },
	{ 0, 0, "sport:free_flying",                           },
	{ 0, 0, "sport:futsal",                                },
	{ 0, 0, "sport:gaelic_games",                          },
	{ 0, 0, "sport:golf",                                  },
	{ 0, 0, "sport:gymnastics",                            },
	{ 0, 0, "sport:handball",                              },
	{ 0, 0, "sport:hapkido",                               },
	{ 0, 0, "sport:horseshoes",                            },
	{ 0, 0, "sport:horse_racing",                          },
	{ 0, 0, "sport:ice_hockey",                            },
	{ 0, 0, "sport:ice_skating",                           },
	{ 0, 0, "sport:ice_stock",                             },
	{ 0, 0, "sport:judo",                                  },
	{ 0, 0, "sport:karate",                                },
	{ 0, 0, "sport:karting",                               },
	{ 0, 0, "sport:kitesurfing",                           },
	{ 0, 0, "sport:korfball",                              },
	{ 0, 0, "sport:lacrosse",                              },
	{ 0, 0, "sport:model_aerodrome",                       },
	{ 0, 0, "sport:motocross",                             },
	{ 0, 0, "sport:motor",                                 },
	{ 0, 0, "sport:multi",                                 },
	{ 0, 0, "sport:netball",                               },
	{ 0, 0, "sport:obstacle_course",                       },
	{ 0, 0, "sport:orienteering",                          },
	{ 0, 0, "sport:paddle_tennis",                         },
	{ 0, 0, "sport:padel",                                 },
	{ 0, 0, "sport:parachuting",                           },
	{ 0, 0, "sport:paragliding",                           },
	{ 0, 0, "sport:pelota",                                },
	{ 0, 0, "sport:racquet",                               },
	{ 0, 0, "sport:rc_car",                                },
	{ 0, 0, "sport:roller_skating",                        },
	{ 0, 0, "sport:rowing",                                },
	{ 0, 0, "sport:rugby_league",                          },
	{ 0, 0, "sport:rugby_union",                           },
	{ 0, 0, "sport:running",                               },
	{ 0, 0, "sport:sailing",                               },
	{ 0, 0, "sport:scuba_diving",                          },
	{ 0, 0, "sport:shooting",                              },
	{ 0, 0, "sport:skateboard",                            },
	{ 0, 0, "sport:soccer",                                },
	{ 0, 0, "sport:sumo",                                  },
	{ 0, 0, "sport:surfing",                               },
	{ 0, 0, "sport:swimming",                              },
	{ 0, 0, "sport:table_tennis",                          },
	{ 0, 0, "sport:table_soccer",                          },
	{ 0, 0, "sport:taekwondo",                             },
	{ 0, 0, "sport:tennis",                                },
	{ 0, 0, "sport:toboggan",                              },
	{ 0, 0, "sport:volleyball",                            },
	{ 0, 0, "sport:water_polo",                            },
	{ 0, 0, "sport:water_ski",                             },
	{ 0, 0, "sport:weightlifting",                         },
	{ 0, 0, "sport:wrestling",                             },
	{ 0, 0, "sport:yoga",                                  },
	{ 0, 0, "tourism:alpine_hut",                          },
	{ 0, 0, "tourism:apartment",                           },
	{ 2, 0, "tourism:aquarium",                            },
	{ 0, 0, "tourism:artwork",                             },
	{ 2, 0, "tourism:attraction",                          },
	{ 0, 0, "tourism:camp_site",                           },
	{ 0, 0, "tourism:caravan_site",                        },
	{ 0, 0, "tourism:chalet",                              },
	{ 0, 0, "tourism:gallery",                             },
	{ 0, 0, "tourism:guest_house",                         },
	{ 0, 0, "tourism:hostel",                              },
	{ 0, 0, "tourism:hotel",                               },
	{ 0, 0, "tourism:information",                         },
	{ 0, 0, "tourism:motel",                               },
	{ 2, 0, "tourism:museum",                              },
	{ 0, 0, "tourism:picnic_site",                         },
	{ 0, 0, "tourism:theme_park",                          },
	{ 0, 0, "tourism:viewpoint",                           },
	{ 0, 0, "tourism:wilderness_hut",                      },
	{ 2, 0, "tourism:zoo",                                 },
	{ 0, 0, "tourism:yes",                                 },
	{ 1, 0, "waterway:river",                              },
	{ 1, 0, "waterway:riverbank",                          },
	{ 1, 0, "waterway:stream",                             },
	{ 0, 0, "waterway:wadi",                               },
	{ 0, 0, "waterway:drystream",                          },
	{ 1, 0, "waterway:canal",                              },
	{ 1, 0, "waterway:drain",                              },
	{ 1, 0, "waterway:ditch",                              },
	{ 0, 0, "waterway:fairway",                            },
	{ 0, 0, "waterway:dock",                               },
	{ 0, 0, "waterway:boatyard",                           },
	{ 3, 0, "waterway:dam",                                },
	{ 3, 0, "waterway:weir",                               },
	{ 0, 0, "waterway:stream_end",                         },
	{ 1, 0, "waterway:waterfall",                          },
	{ 0, 0, "waterway:lock_gate",                          },
	{ 0, 0, "waterway:turning_point",                      },
	{ 0, 0, "waterway:water_point",                        },
	{ 0, 0, "waterway:fuel",                               },
	{ 0, 0, "core:wilderness",                             },
	{ 0, 0, "core:recreation",                             },
	{ 0, 0, "core:special",                                },
	{ 0, 0, "core:mineral",                                },
	{ 0, 0, "default:waypoint",                            },
	{ 0, 0, "core:coal_methane",                           },
	{ 0, 0, "core:historic",                               },
	{ 0, 0, "rec:wilderness",                              },
	{ 0, 0, "rec:special",                                 },
	{ 0, 0, "rec:mineral",                                 },
	{ 0, 0, "craft:parquet_layer",                         },
	{ 0, 0, NULL                                           },
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

int osmdb_classCodeToRank(int code)
{
	int count = osmdb_classCount();
	if(code < count)
	{
		return OSM_UTIL_CLASSES[code].rank;
	}
	return OSM_UTIL_CLASSES[0].rank;
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

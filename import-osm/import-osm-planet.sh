#!/bin/bash

unbuffer ./osmdb/import-osm/import-osm 4.0 osmdb/style/default.xml planet.osm planet.sqlite3 | tee import-osm-planet.log

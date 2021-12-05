#!/bin/bash

./osmdb/import-osm/import-osm osmdb/style/default.xml planet.osm planet.sqlite3 | tee import-osm-planet.log

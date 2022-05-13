#!/bin/bash

./osmdb/import-osm/import-osm 2.0 osmdb/style/default.xml CO.osm CO.sqlite3 | tee import-osm-CO.log

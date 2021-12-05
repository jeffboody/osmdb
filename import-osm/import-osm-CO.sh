#!/bin/bash

./osmdb/import-osm/import-osm osmdb/style/default.xml CO.osm CO.sqlite3 | tee import-osm-CO.log

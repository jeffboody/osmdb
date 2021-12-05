#!/bin/bash

./osmdb/import-osm/import-osm osmdb/style/default.xml Boulder.osm Boulder.sqlite3 | tee import-osm-Boulder.log

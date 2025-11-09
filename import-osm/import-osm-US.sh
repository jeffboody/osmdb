#!/bin/bash

unbuffer ./osmdb/import-osm/import-osm 4.0 osmdb/style/default.xml US.osm US.sqlite3 | tee import-osm-US.log

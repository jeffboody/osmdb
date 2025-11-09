#!/bin/bash

unbuffer ./osmdb/import-kml/import-kml 4.0 osmdb/style/default.xml US.sqlite3 States/cb_2018_us_state_500k.kml | tee import-kml-US.log

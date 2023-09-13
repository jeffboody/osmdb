OSMDB
=====

This repository contains a set of tools for manipulating
the OpenStreetMap database.

OSM Data
========

Download Planet / Changeset

	https://planet.openstreetmap.org/

	wget https://planet.openstreetmap.org/pbf/planet-latest.osm.pbf
	wget https://planet.openstreetmap.org/planet/changesets-latest.osm.bz2

Download Osmosis

	http://wiki.openstreetmap.org/wiki/Osmosis#Latest_stable_version

	croot
	mkdir osmosis
	cd osmosis
	wget http://bretth.dev.openstreetmap.org/osmosis-build/osmosis-latest.tgz
	tar -xzf osmosis-latest.tgz

	sudo apt-get install openjdk-8-jdk

Optionally crop the Planet (e.g.)

	osmosis/bin/osmosis --read-pbf planet-latest.osm.pbf --write-xml planet.osm
	osmosis/bin/osmosis --read-pbf planet-latest.osm.pbf --bounding-box top=72.0 left=-170.0 bottom=18.0 right=-66.0 --write-xml US.osm
	osmosis/bin/osmosis --read-pbf planet-latest.osm.pbf --bounding-box top=51.0 left=-126.0 bottom=23.0 right=-64.0 --write-xml US48.osm
	osmosis/bin/osmosis --read-xml US48.osm --bounding-box top=43.0 left=-110.0 bottom=34.0 right=-100.0 --write-xml CO.osm
	osmosis/bin/osmosis --read-xml CO.osm --bounding-box top=40.1 left=-105.4 bottom=39.9 right=-105.1 --write-xml Boulder.osm

Import OSM
==========

To import planet.osm to sqlite3.

	import-osm-planet.sh

Import KML
==========

Download state shapefile (cb_2018_us_state_500k.zip):

	https://www.census.gov/geographies/mapping-files/time-series/geo/carto-boundary-file.html

Convert shapefiles to KML files (https://www.igismap.com/shp-to-kml/).

	sudo apt install gdal-bin
	ogr2ogr -f KML cb_2018_us_state_500k.kml cb_2018_us_state_500k.shp
	ogr2ogr -f KML CORE_Act.kml CORE_Act.shp
	ogr2ogr -f KML CORE_Act.kml CORE_Act.shp
	ogr2ogr -f KML REC_Act.kml REC_Act.shp

To import optional kml files.

	import-kml-planet.sh

Prefetch
========

To prefetch osmdb tiles.

	prefetch-US.sh

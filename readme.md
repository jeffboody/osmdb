OSMDB
=====

This repository contains a set of tools for manipulating
the OpenStreetMap database.

OSM Data
========

download planet

	wget https://ftp.osuosl.org/pub/openstreetmap/pbf/planet-latest.osm.pbf

download osmosis

	# http://wiki.openstreetmap.org/wiki/Osmosis#Latest_stable_version
	croot
	mkdir osmosis
	cd osmosis
	wget http://bretth.dev.openstreetmap.org/osmosis-build/osmosis-latest.tgz
	tar -xzf osmosis-latest.tgz

	sudo apt-get install openjdk-8-jdk

refer to this site to determine lat/lon bounding box

	http://pos-map.appspot.com/en/coordinates10.html

crop planet (e.g.)

	./osmosis/bin/osmosis --read-pbf planet-latest.osm.pbf --bounding-box top=72.0 left=-170.0 bottom=18.0 right=-66.0 --write-xml US-base.osm
	./osmosis/bin/osmosis --read-pbf planet-latest.osm.pbf --bounding-box top=51.0 left=-126.0 bottom=23.0 right=-64.0 --write-xml US48-base.osm
	./osmosis/bin/osmosis --read-xml US48-base.osm --bounding-box top=43.0 left=-110.0 bottom=34.0 right=-100.0 --write-xml CO-base.osm
	./osmosis/bin/osmosis --read-xml CO-base.osm --bounding-box top=40.1 left=-105.4 bottom=39.9 right=-105.1 --write-xml Boulder-base.osm

reformat osm data (e.g.)

	croot

	unaccent UTF-8 < US48-base.osm > US48-unaccent.osm
	./bin/clean-symbols.sh US48-unaccent.osm US48.osm
	osmdb-build US48 | tee build.log
	osmdb-indexer filter/default.xml US48 | tee indexer.log
	osmdb-tiler . US48 | tee tiler.log

	unaccent UTF-8 < CO-base.osm > CO-unaccent.osm
	./bin/clean-symbols.sh CO-unaccent.osm CO.osm
	osmdb-build CO | tee build.log
	osmdb-indexer filter/default.xml CO | tee indexer.log
	osmdb-tiler . CO | tee tiler.log

	unaccent UTF-8 < Boulder-base.osm > Boulder-unaccent.osm
	./bin/clean-symbols.sh Boulder-unaccent.osm Boulder.osm
	osmdb-build Boulder | tee build.log
	osmdb-indexer filter/default.xml Boulder | tee indexer.log
	osmdb-tiler . Boulder | tee tiler.log

SQLite
======

To initialize SQLite3 Database:

	./osmdb-sqlite BASE
	./shell BASE.sqlite3
	> .read init.sql
	> .mode csv
	> .separator |
	> .import BASE-tbl_nodes.data tbl_nodes
	> .import BASE-tbl_ways.data tbl_ways
	> .import BASE-tbl_rels.data tbl_rels
	> .import BASE-tbl_ways_nds.data tbl_ways_nds
	> .import BASE-tbl_nodes_members.data tbl_nodes_members
	> .import BASE-tbl_ways_members.data tbl_ways_members
	> .import BASE-tbl_nodes_selected.data tbl_nodes_selected
	> .import BASE-tbl_ways_selected.data tbl_ways_selected
	> .import BASE-tbl_ways_center.data tbl_ways_center
	> .import BASE-tbl_rels_center.data tbl_rels_center
	> .import BASE-tbl_ways_polygon.data tbl_ways_polygon
	> .import BASE-tbl_rels_polygon.data tbl_rels_polygon
	> .read process.sql

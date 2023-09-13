#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=US 4.0 osmdbv10-US.bfs planet.sqlite3 | tee prefetch-US.log

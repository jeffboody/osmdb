#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=US 4.0 osmdbv8-US.bfs planet.sqlite3 | tee prefetch-US.log

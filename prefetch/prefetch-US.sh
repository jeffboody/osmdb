#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=US 4.0 osmdbv12-US.bfs US.sqlite3 | tee prefetch-US.log

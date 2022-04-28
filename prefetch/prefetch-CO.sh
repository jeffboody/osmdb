#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=CO 2.0 osmdbv7-CO.bfs CO.sqlite3 | tee prefetch-CO.log

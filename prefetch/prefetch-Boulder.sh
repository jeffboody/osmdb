#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=CO 2.0 osmdbv6-Boulder.bfs Boulder.sqlite3 | tee prefetch-Boulder.log

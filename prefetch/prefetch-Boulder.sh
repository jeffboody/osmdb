#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=CO 2.0 osmdbv9-Boulder.bfs Boulder.sqlite3 | tee prefetch-Boulder.log

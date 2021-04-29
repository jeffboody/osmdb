#!/bin/bash

LD_PRELOAD=/usr/local/lib/libjemalloc.so unbuffer ./osmdb-prefetch -pf=CO 2.0 osmdbv5-CO.sqlite3 planet.sqlite3 | tee log-CO.txt

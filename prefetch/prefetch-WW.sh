#!/bin/bash

LD_PRELOAD=/usr/local/lib/libjemalloc.so unbuffer ./osmdb-prefetch -pf=WW 4.0 cache-WW.sqlite3 planet.sqlite3 | tee log-WW.txt
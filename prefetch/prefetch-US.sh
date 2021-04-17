#!/bin/bash

LD_PRELOAD=/usr/local/lib/libjemalloc.so unbuffer ./osmdb-prefetch -pf=US 4.0 cache-US.sqlite3 planet.sqlite3 | tee log-US.txt

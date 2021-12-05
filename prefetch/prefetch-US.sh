#!/bin/bash

unbuffer ./osmdb/prefetch/osmdb-prefetch -pf=US 4.0 osmdbv6-US.sqlite3 planet.sqlite3 | tee prefetch-US.log

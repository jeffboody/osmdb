cmake_minimum_required(VERSION 3.4.1)

# Compiler options
add_compile_options(-Wall)

# Submodule library
add_library(osmdb
            STATIC

            # Source
            osmdb_index.c
            osmdb_chunk.c
            osmdb_parser.c
            osmdb_style.c
            osmdb_util.c
            osmdb_node.c
            osmdb_way.c
            osmdb_relation.c
            osmdb_tile.c
            osmdb_range.c)

# Linking
target_link_libraries(osmdb

                      # Submodule libraries
                      a3d

                      # NDK libraries
                      log)

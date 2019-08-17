cmake_minimum_required(VERSION 3.4.1)

# Defines
set(CC_USE_MATH true)

# Compiler options
add_compile_options(-Wall)

# Submodule library
add_library(osmdb
            STATIC

            # Source
            osmdb_parser.c
            osmdb_style.c
            osmdb_util.c
            osmdb_node.c
            osmdb_way.c
            osmdb_relation.c)

# Linking
target_link_libraries(osmdb

                      # Submodule libraries
                      cc

                      # NDK libraries
                      log)

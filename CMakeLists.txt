cmake_minimum_required(VERSION 3.4.1)

# Defines
set(CC_USE_MATH true)

# Compiler options
add_compile_options(-Wall)

# Submodule library
add_library(osmdb
            STATIC

            # Source
            osmdb_style.c
            osmdb_util.c
            tiler/osmdb_tile.c)

# Linking
target_link_libraries(osmdb

                      # Submodule libraries
                      cc

                      # NDK libraries
                      log)

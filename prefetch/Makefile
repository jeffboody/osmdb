export CC_USE_MATH = 1

TARGET   = osmdb-prefetch
CLASSES  = osmdb/index/osmdb_index osmdb/index/osmdb_type osmdb/index/osmdb_entry \
           osmdb/tiler/osmdb_tiler osmdb/tiler/osmdb_tile osmdb/tiler/osmdb_tilerState osmdb/tiler/osmdb_waySegment osmdb/tiler/osmdb_ostream \
           osmdb/osmdb_range osmdb/osmdb_util
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall
CFLAGS   = $(OPT) -I.
LDFLAGS  = -Llibsqlite3 -lsqlite3 -Lterrain -lterrain -Llibbfs -lbfs -Llibcc -lcc -ldl -lpthread -lm
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libbfs libcc libsqlite3 terrain
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libbfs libcc libsqlite3 terrain

libbfs:
	$(MAKE) -C libbfs

libcc:
	$(MAKE) -C libcc

libsqlite3:
	$(MAKE) -C libsqlite3

terrain:
	$(MAKE) -C terrain

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C libbfs clean
	$(MAKE) -C libcc clean
	$(MAKE) -C libsqlite3 clean
	$(MAKE) -C terrain clean
	rm libbfs libcc libsqlite3 osmdb terrain

$(OBJECTS): $(HFILES)

export CC_USE_MATH = 1

TARGET   = osmdb-select
CLASSES  = osmdb/index/osmdb_index osmdb/index/osmdb_type osmdb/index/osmdb_entry \
           osmdb/tiler/osmdb_tiler osmdb/tiler/osmdb_tile osmdb/tiler/osmdb_tilerState osmdb/tiler/osmdb_waySegment osmdb/tiler/osmdb_ostream \
           osmdb/osmdb_range osmdb/osmdb_util
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall -Wno-format-truncation
CFLAGS   = $(OPT) -I.
LDFLAGS  = -Llibsqlite3 -lsqlite3 -Llibbfs -lbfs -Llibcc -lcc -Lterrain -lterrain -Ltexgz -l texgz -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -ldl -lpthread -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libbfs libcc terrain texgz xmlstream libexpat libsqlite3
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libbfs libcc terrain texgz xmlstream libexpat libsqlite3

libbfs:
	$(MAKE) -C libbfs

libcc:
	$(MAKE) -C libcc

terrain:
	$(MAKE) -C terrain

texgz:
	$(MAKE) -C texgz

xmlstream:
	$(MAKE) -C libxmlstream

libexpat:
	$(MAKE) -C libexpat/expat/lib

libsqlite3:
	$(MAKE) -C libsqlite3

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C libbfs clean
	$(MAKE) -C libcc clean
	$(MAKE) -C terrain clean
	$(MAKE) -C texgz clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	$(MAKE) -C libsqlite3 clean
	rm osmdb libbfs libcc texgz terrain libexpat libxmlstream libsqlite3

$(OBJECTS): $(HFILES)

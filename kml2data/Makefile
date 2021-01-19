export CC_USE_MATH = 1

TARGET   = kml2data
CLASSES  = kml_parser osmdb/index/osmdb_index osmdb/index/osmdb_blob osmdb/index/osmdb_entry osmdb/osmdb_filter osmdb/osmdb_util
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall -Wno-format-truncation
CFLAGS   = $(OPT) -I. -DOSMDB_IMPORTER
LDFLAGS  = -Llibsqlite3 -lsqlite3 -Llibcc -lcc -Lterrain -lterrain -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -ldl -lpthread -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libsqlite3 libcc terrain xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libsqlite3 libcc terrain xmlstream libexpat

libcc:
	$(MAKE) -C libcc -f Makefile

terrain:
	$(MAKE) -C terrain

xmlstream:
	$(MAKE) -C libxmlstream

libexpat:
	$(MAKE) -C libexpat/expat/lib

libsqlite3:
	$(MAKE) -C libsqlite3

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C libcc -f Makefile clean
	$(MAKE) -C terrain
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	$(MAKE) -C libsqlite3 clean
	rm libsqlite3 libcc terrain osmdb libxmlstream libexpat

$(OBJECTS): $(HFILES)

export CC_USE_MATH = 1

TARGET   = import-kml
CLASSES  = kml_parser osmdb/index/osmdb_index osmdb/index/osmdb_type osmdb/index/osmdb_entry osmdb/osmdb_util osmdb/osmdb_style
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall -Wno-format-truncation
CFLAGS   = $(OPT) -I. -DOSMDB_IMPORTER
LDFLAGS  = -Llibsqlite3 -lsqlite3 -Llibbfs -lbfs -Llibcc -lcc -Lterrain -lterrain -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -ldl -lpthread -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libsqlite3 libbfs libcc terrain xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libsqlite3 libbfs libcc terrain xmlstream libexpat

libbfs:
	$(MAKE) -C libbfs -f Makefile

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
	$(MAKE) -C libbfs -f Makefile clean
	$(MAKE) -C libcc -f Makefile clean
	$(MAKE) -C terrain
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	$(MAKE) -C libsqlite3 clean
	rm libsqlite3 libbfs libcc terrain osmdb libxmlstream libexpat

$(OBJECTS): $(HFILES)

TARGET   = osmdb-indexer
CLASSES  = ../osmdb_util ../osmdb_node ../osmdb_way ../osmdb_relation ../osmdb_chunk ../osmdb_index ../osmdb_parser
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall
CFLAGS   = $(OPT) -I.
LDFLAGS  = -La3d -la3d -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) a3d xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: a3d xmlstream libexpat

a3d:
	$(MAKE) -C a3d -f Makefile.sdl

xmlstream:
	$(MAKE) -C libxmlstream

libexpat:
	$(MAKE) -C libexpat/expat/lib

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C a3d -f Makefile.sdl clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	rm a3d libxmlstream libexpat

$(OBJECTS): $(HFILES)

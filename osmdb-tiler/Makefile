export CC_USE_MATH = 1

TARGET   = osmdb-tiler
CLASSES  = ../osmdb_index ../osmdb_chunk ../osmdb_tile ../osmdb_filter ../osmdb_range
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall -Wno-format-truncation
CFLAGS   = $(OPT) -I. `sdl2-config --cflags` -DA3D_GL2 -DGOTO_USE_SDL
LDFLAGS  = -Losmdb -losmdb -Lterrain -lterrain -Llibcc -lcc -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) terrain libcc osmdb xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: terrain libcc osmdb xmlstream libexpat

terrain:
	$(MAKE) -C terrain

libcc:
	$(MAKE) -C libcc

xmlstream:
	$(MAKE) -C libxmlstream

osmdb:
	$(MAKE) -C osmdb

libexpat:
	$(MAKE) -C libexpat/expat/lib

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C terrain clean
	$(MAKE) -C libcc clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C osmdb clean
	$(MAKE) -C libexpat/expat/lib clean
	rm terrain libcc osmdb libxmlstream libexpat

$(OBJECTS): $(HFILES)

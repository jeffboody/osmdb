export CC_USE_MATH = 1

TARGET   = osmdb-sqlite
CLASSES  = osm_parser ../osmdb_filter ../osmdb_util ../osmdb_style
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall
CFLAGS   = $(OPT) -I.
LDFLAGS  = -Llibcc -lcc -Lterrain -lterrain -Llibpak -lpak -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libcc terrain libpak xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libcc terrain libpak xmlstream libexpat

libcc:
	$(MAKE) -C libcc

terrain:
	$(MAKE) -C terrain

libpak:
	$(MAKE) -C libpak

xmlstream:
	$(MAKE) -C libxmlstream

libexpat:
	$(MAKE) -C libexpat/expat/lib

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C libcc clean
	$(MAKE) -C terrain clean
	$(MAKE) -C libpak clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	rm libcc terrain libpak libxmlstream libexpat

$(OBJECTS): $(HFILES)

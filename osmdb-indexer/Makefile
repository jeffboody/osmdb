TARGET   = osmdb-indexer
CLASSES  =
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall
CFLAGS   = $(OPT) -I.
LDFLAGS  = -Losmdb -losmdb -Lterrain -lterrain -La3d -la3d -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) terrain a3d osmdb xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: terrain a3d osmdb xmlstream libexpat

terrain:
	$(MAKE) -C terrain

a3d:
	$(MAKE) -C a3d -f Makefile.sdl

xmlstream:
	$(MAKE) -C libxmlstream

osmdb:
	$(MAKE) -C osmdb

libexpat:
	$(MAKE) -C libexpat/expat/lib

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C terrain clean
	$(MAKE) -C a3d -f Makefile.sdl clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C osmdb clean
	$(MAKE) -C libexpat/expat/lib clean
	rm terrain a3d osmdb libxmlstream libexpat

$(OBJECTS): $(HFILES)

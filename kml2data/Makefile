TARGET   = kml2data
CLASSES  = kml_parser
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall
CFLAGS   = $(OPT) -I.
LDFLAGS  = -Llibcc -lcc -Lterrain -lterrain -Losmdb -losmdb -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libcc terrain osmdb xmlstream libexpat
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libcc terrain osmdb xmlstream libexpat

libcc:
	$(MAKE) -C libcc -f Makefile

terrain:
	$(MAKE) -C terrain -f Makefile

osmdb:
	$(MAKE) -C osmdb

xmlstream:
	$(MAKE) -C libxmlstream

libexpat:
	$(MAKE) -C libexpat/expat/lib

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C libcc -f Makefile clean
	$(MAKE) -C terrain -f Makefile clean
	$(MAKE) -C osmdb clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	rm libcc terrain osmdb libxmlstream libexpat

$(OBJECTS): $(HFILES)

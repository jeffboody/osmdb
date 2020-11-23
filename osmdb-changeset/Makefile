export CC_USE_MATH = 1

TARGET   = osmdb-changeset
CLASSES  = osm_parser
SOURCE   = $(TARGET).c $(CLASSES:%=%.c)
OBJECTS  = $(TARGET).o $(CLASSES:%=%.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall
CFLAGS   = $(OPT) -I.
LDFLAGS  = -Llibsqlite3 -lsqlite3 -Llibcc -lcc -Llibxmlstream -lxmlstream -Llibexpat/expat/lib -lexpat -ldl -lpthread -lm -lz
CCC      = gcc

all: $(TARGET)

$(TARGET): $(OBJECTS) libcc xmlstream libexpat libsqlite3
	$(CCC) $(OPT) $(OBJECTS) -o $@ $(LDFLAGS)

.PHONY: libcc xmlstream libexpat libsqlite3

libcc:
	$(MAKE) -C libcc

xmlstream:
	$(MAKE) -C libxmlstream

libexpat:
	$(MAKE) -C libexpat/expat/lib

libsqlite3:
	$(MAKE) -C libsqlite3

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	$(MAKE) -C libcc clean
	$(MAKE) -C libxmlstream clean
	$(MAKE) -C libexpat/expat/lib clean
	$(MAKE) -C libsqlite3 clean
	rm libcc libxmlstream libexpat libsqlite3

$(OBJECTS): $(HFILES)

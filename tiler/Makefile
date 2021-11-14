export CC_USE_MATH = 1

TARGET   = libosmdb_tiler.a
CLASSES  = osmdb_tiler osmdb_tilerState osmdb_tile osmdb_ostream osmdb_waySegment
SOURCE   = $(CLASSES:%=%.c)
OBJECTS  = $(SOURCE:.c=.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall -Wno-format-truncation
CFLAGS   = $(OPT) -I.
LDFLAGS  = -lm
AR       = ar

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)
	rm libcc libsqlite3 libxmlstream terrain

$(OBJECTS): $(HFILES)

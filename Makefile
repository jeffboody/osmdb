TARGET   = libosmdb.a
CLASSES  = osmdb_parser osmdb_style osmdb_util osmdb_node osmdb_way osmdb_relation
SOURCE   = $(CLASSES:%=%.c)
OBJECTS  = $(SOURCE:.c=.o)
HFILES   = $(CLASSES:%=%.h)
OPT      = -O2 -Wall -Wno-format-truncation
CFLAGS   = $(OPT) -I. -DA3D_GL2
LDFLAGS  = -lm
AR       = ar

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

clean:
	rm -f $(OBJECTS) *~ \#*\# $(TARGET)

$(OBJECTS): $(HFILES)

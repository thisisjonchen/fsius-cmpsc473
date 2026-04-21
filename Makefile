CC      = gcc
CFLAGS  = -Wall -Wextra -O2
AR      = ar
ARFLAGS = rcs

PARSER_LIB    = libparser.a
GENERATOR_LIB = libgenerator.a

.PHONY: all clean libs

all: driver

libs: $(PARSER_LIB) $(GENERATOR_LIB)

# --- Parser library ---
parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c -o parser.o

$(PARSER_LIB): parser.o
	$(AR) $(ARFLAGS) $@ $^

# --- Generator library ---
generator.o: generator.c generator.h
	$(CC) $(CFLAGS) -c generator.c -o generator.o

$(GENERATOR_LIB): generator.o
	$(AR) $(ARFLAGS) $@ $^

# --- Driver / wrapper ---
driver.o: driver.c parser.h generator.h
	$(CC) $(CFLAGS) -c driver.c -o driver.o

driver: driver.o $(PARSER_LIB) $(GENERATOR_LIB)
	$(CC) $(CFLAGS) driver.o $(PARSER_LIB) $(GENERATOR_LIB) -o driver

clean:
	rm -f *.o *.a driver

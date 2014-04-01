#CC=gcc
CFLAGS=-g -c -Wall
LDFLAGS=-g
SOURCES=liang_sha1.c test_liang_sha1.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=test_liang_sha1

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

run: $(EXECUTABLE)
	./$(EXECUTABLE)

.PHONY: clean
clean:
	rm $(OBJECTS) $(EXECUTABLE) *~
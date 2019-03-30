CFLAGS := $(CFLAGS) --std=c11 -Wall -Werror=vla -O3
INCLUDE_DIRS=\
	$(shell pkg-config --cflags uthash) \
	-I$(CURDIR)/include

.PHONY: all
all: examples/nested_conditions examples/simple_error

src/exceptions.o: src/exceptions.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $^ -o $@

examples/nested_conditions.o: examples/nested_conditions.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $^ -o $@

examples/nested_conditions: examples/nested_conditions.o src/exceptions.o
	$(CC) -o $@ $^

examples/simple_error.o: examples/simple_error.c
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -c $^ -o $@

examples/simple_error: examples/simple_error.o src/exceptions.o
	$(CC) -o $@ $^

.PHONY: clean
clean:
	rm examples/*.o
	rm src/*.o

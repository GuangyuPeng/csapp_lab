# Makefile for Proxy Lab 

INC_DIR = include
TEST_DIR = test
CC = gcc
CFLAGS = -O2 -Wall -I$(INC_DIR)
LDFLAGS = -lpthread
OBJS = csapp.o http.o cache.o proxy.o
SRCS = $(OBJS:.o=.c)
TEST_SRCS = $(TEST_DIR)/test_cache.c $(TEST_DIR)/test_http.c
TEST_OBJS = $(TEST_SRCS:.c=.o)
TEST_EXES = $(patsubst %.c, %, $(TEST_SRCS))

TEST_DEPS = $(TEST_SRCS:.c=.d)
DEPS = $(SRCS:.c=.d)

all: proxy

proxy: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o proxy $(LDFLAGS)

test: $(TEST_EXES)

test/test_http: $(TEST_DIR)/test_http.o http.o
	$(CC) $(CFLAGS) $(TEST_DIR)/test_http.o http.o -o $@

test/test_cache: $(TEST_DIR)/test_cache.o cache.o csapp.o
	$(CC) $(CFLAGS) $(TEST_DIR)/test_cache.o cache.o csapp.o -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_DEPS): %.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $(CFLAGS) $< > $@.$$$$; \
	sed 's,\(^.\+\)\.o\s*:\s*,$(TEST_DIR)/\1.o $@: ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

$(DEPS): %.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $(CFLAGS) $< > $@.$$$$; \
	sed 's,\(^.\+\)\.o\s*:\s*,\1.o $@: ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

include $(DEPS)
include $(TEST_DEPS)

.PHONY: clean
clean:
	rm -f *~ *.o proxy core *.tar *.zip *.gzip *.bzip *.gz
	rm -f $(DEPS)
	rm -f $(TEST_DEPS)
	rm -f $(TEST_OBJS)
	rm -f $(TEST_EXES)

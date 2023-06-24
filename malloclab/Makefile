#
# Students' Makefile for the Malloc Lab
#

INC_DIR = include
TIMER_DIR = timer
MEM_DIR = mem
OBJ_DIR = objs
CC = gcc
CFLAGS = -Wall -O2 -m32 -I$(INC_DIR)

TAR_OBJS = $(OBJ_DIR)/mdriver.o $(OBJ_DIR)/mm.o
MEM_OBJS = $(OBJ_DIR)/memlib.o
TIMER_OBJS = $(OBJ_DIR)/fsecs.o $(OBJ_DIR)/fcyc.o \
	$(OBJ_DIR)/clock.o $(OBJ_DIR)/ftimer.o
OBJS = $(TAR_OBJS) $(MEM_OBJS) $(TIMER_OBJS)

TAR_SRCS = $(TAR_OBJS:$(OBJ_DIR)/%.o=%.c)
MEM_SRCS = $(MEM_OBJS:$(OBJ_DIR)/%.o=$(MEM_DIR)/%.c)
TIMER_SRCS = $(TIMER_OBJS:$(OBJ_DIR)/%.o=$(TIMER_DIR)/%.c)
SRCS = $(TAR_SRCS) $(MEM_SRCS) $(TIMER_SRCS)

mdriver: $(OBJS)
	$(CC) $(CFLAGS) -o mdriver $(OBJS)

$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ_DIR)/%.o: $(MEM_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJ_DIR)/%.o: $(TIMER_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -MM $(CPPFLAGS) $(CFLAGS) $< > $@.$$$$; \
	sed 's,\(^.\+\)\.o\s*:\s*,$(OBJ_DIR)/\1.o $@: ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

DEPS = $(SRCS:.c=.d)
include $(DEPS)

.PHONY: clean
clean:
	rm -f $(DEPS)
	rm -f *~ *.o $(OBJ_DIR)/*~ $(OBJ_DIR)/*.o mdriver

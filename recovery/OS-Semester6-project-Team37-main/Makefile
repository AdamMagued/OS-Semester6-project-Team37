CC      = gcc
CFLAGS  = -Wall -Wextra -g
SRCDIR  = src
BINDIR  = bin

SRCS    = $(SRCDIR)/SchedulerOS.c \
          $(SRCDIR)/memory.c \
          $(SRCDIR)/interpreter.c \
          $(SRCDIR)/mutex.c \
          $(SRCDIR)/syscalls.c

TARGET  = $(BINDIR)/scheduler

.PHONY: all clean run

all: $(BINDIR) $(TARGET)

$(BINDIR):
	mkdir -p $(BINDIR)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

run: all
	./$(TARGET)

clean:
	rm -rf $(BINDIR)

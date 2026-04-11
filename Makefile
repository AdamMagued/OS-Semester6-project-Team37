CC = gcc
CFLAGS = -Wall -g
OBJS = SchedulerOS.o memory.o mutex.o interpreter.o
TARGET = os_sim

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

SchedulerOS.o: SchedulerOS.c memory.h mutex.h interpreter.h
	$(CC) $(CFLAGS) -c SchedulerOS.c

memory.o: memory.c memory.h
	$(CC) $(CFLAGS) -c memory.c

mutex.o: mutex.c mutex.h memory.h
	$(CC) $(CFLAGS) -c mutex.c

interpreter.o: interpreter.c interpreter.h memory.h mutex.h
	$(CC) $(CFLAGS) -c interpreter.c

clean:
	rm -f *.o $(TARGET)

.PHONY: clean

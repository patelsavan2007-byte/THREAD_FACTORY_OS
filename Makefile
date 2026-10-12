CC = gcc
CFLAGS = -Wall -Wextra -O2 -Ibackend
TARGET = factory_server

ifeq ($(OS),Windows_NT)
    LIBS = -lpthread -lws2_32
    TARGET := $(TARGET).exe
    RM = del /Q /F
else
    LIBS = -lpthread
    RM = rm -f
endif

SRCS = backend/main.c backend/queue.c backend/worker_pool.c backend/http_server.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LIBS) -o $(TARGET)

clean:
	$(RM) $(TARGET) *.o backend/*.o

run: $(TARGET)
	./$(TARGET) 8080

.PHONY: all clean run

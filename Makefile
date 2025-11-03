# Makefile for the Gemini C File Manager

CC = gcc
CFLAGS = -I/opt/homebrew/include `pkg-config --cflags gtk+-3.0` -Wall
LIBS = -L/opt/homebrew/lib `pkg-config --libs gtk+-3.0` -lzip

TARGET = filemanager
SRCS = main.c backend.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean

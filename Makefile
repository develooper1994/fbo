# Define the compiler, defaulting to gcc. !!!Change as you wish!!!
CC ?= gcc

# Define the target executable
TARGET = main

# Define the source files
SRCS = main.c

# Define the object files
OBJS = $(SRCS:.c=.o)

# Define the flags. !!!Change as you wish!!!
CFLAGS = -Wall -Wextra -O2

# Define the default rule
all: $(TARGET)

# Rule to link the object files into the target executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

# Rule to compile the source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule to remove generated files
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean

CC := cc
CFLAGS := -O0 -g -Wall -Wextra -pthread
VG := valgrind
VFLAGS := --show-leak-kinds=all --leak-check=full --verbose

SRCDIR := ./src/
OBJS := list.o threadpool.o
TEST := main.o

OBJDIR := ${addprefix $(SRCDIR), $(OBJS)}
OBJDIR += $(TEST)

BIN := main


.PHONY: all run check clean clean_all

all: $(OBJDIR)
	$(CC) $(CFLAGS) $(OBJDIR) -o $(BIN)

run: all
	./$(BIN)

check: all
	$(VG) $(VFLAGS) ./$(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJDIR)

clean_all: clean
	rm -f $(BIN)
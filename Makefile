CC := cc
DBG := -O0 -g
CFLAGS := -Wall -Wextra -pthread -D_REENTRANT

VG := valgrind
VFLAGS := --show-leak-kinds=all --leak-check=full --verbose

SRCDIR := ./src
LIBDIR := ./lib
BINDST := ./bin
OBJS := list.o threadpool.o
TEST := main.o

OBJDIR := ${addprefix $(SRCDIR)/, $(OBJS)}

BIN := main

STATICLIB := threadpool.a
SHAREDLIB := threadpool.so

ifdef DEBUG
	CFLAGS += $(DBG)
else
	CFLAGS += -DNDEBUG
endif

# TODO: 
#  	build from src, compile into obj,
#	link to lib, executable in bin

.PHONY: all run check clean clean_all
_ := $(shell mkdir -p $(BINDST))

all: test_build static shared

test_build: $(OBJDIR) $(TEST)
	$(CC) $(CFLAGS) $^ -o $(BINDST)/$(BIN)

create_libdir:
	@mkdir -p $(LIBDIR)

static: $(OBJDIR) create_libdir
	ar rcs $(LIBDIR)/$(STATICLIB) $(OBJDIR)

shared: $(OBJDIR) create_libdir
	$(CC) -fPIC -shared $(OBJDIR) -o $(LIBDIR)/$(SHAREDLIB)

run: test_build
	./$(BINDST)/$(BIN)

check: test_build
	$(VG) $(VFLAGS) ./$(BINDST)/$(BIN)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJDIR) $(TEST)

clean_all: clean
	rm -f ./$(BINDST)/$(BIN) $(LIBDIR)/$(STATICLIB) $(LIBDIR)/$(SHAREDLIB)

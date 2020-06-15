CC := cc
AR := ar
VG := valgrind

CFLAGS := -Wall -Wextra -pthread -D_REENTRANT
CREL_NOWARN = -Wno-unused-variable -Wno-unused-parameter
CREL_FLAGS = -O2 -DNDEBUG $(CREL_NOWARN)
CDBG_FLAGS := -g -O0

LDFLAGS = -Wl,--version-script=$(CFGDIR)/$(VERSCRIPT)
VERSCRIPT := version.map

ARFLAGS := -rcs

VGFLAGS := --leak-check=full --show-leak-kinds=all --verbose


PROJ_NAME := sthp

INCDIR := include
SRCDIR := src
TSTDIR := test
LIBDIR := lib
OBJDIR := obj
BINDIR := bin
CFGDIR := cfg

CFLAGS += -I./$(INCDIR)

ifeq ($(DEBUG), 1)
	CFLAGS += $(CDBG_FLAGS)
else
	CFLAGS += $(CREL_FLAGS)
endif


CORE_OBJS := threadpool.o gate.o list.o
TEST_OBJS := main.o

BIN_NAME := $(PROJ_NAME)-test
LIBSTATIC := lib$(PROJ_NAME).a
LIBSHARED := lib$(PROJ_NAME).so

CORE_OBJ_DST := $(addprefix $(OBJDIR)/,$(CORE_OBJS))
TEST_OBJ_DST := $(addprefix $(OBJDIR)/,$(TEST_OBJS))

LIBSTATIC_DST := $(LIBDIR)/$(LIBSTATIC)
LIBSHARED_DST := $(LIBDIR)/$(LIBSHARED)
BINDST := $(BINDIR)/$(BIN_NAME)

.PHONY: clean clean_all
all: shared static buildtest

$(CORE_OBJ_DST) $(TEST_OBJ_DST): |$(OBJDIR)
$(OBJDIR):
	mkdir -p ./$(OBJDIR) ./$(LIBDIR) ./$(BINDIR)

# --- start build objs ---
$(CORE_OBJ_DST): $(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $^

$(TEST_OBJ_DST): $(OBJDIR)/%.o: $(TSTDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $^
# --- end build objs ---
static: $(LIBSTATIC_DST)
shared: $(LIBSHARED_DST)

# TODO: add `LDFLAGS`.
$(LIBSHARED_DST): CFLAGS += -fPIC
$(LIBSHARED_DST): $(CORE_OBJ_DST)
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -o $@ $^

$(LIBSTATIC_DST): $(CORE_OBJ_DST)
	$(AR) $(ARFLAGS) $@ $^

$(BINDST): $(TEST_OBJ_DST) $(LIBSHARED_DST)
	$(CC) $(CFLAGS) -o $@ -Wl,-rpath=./$(LIBDIR) $^

buildtest: $(BINDST)

runtest: buildtest
	./$(BINDST)

runcheck: buildtest
	$(VG) $(VGFLAGS) ./$(BINDST)

clean:
	rm -f $(CORE_OBJ_DST)
	rm -f $(TEST_OBJ_DST)

clean_all: clean
	rm -f $(LIBSTATIC_DST)
	rm -f $(LIBSHARED_DST)
	rm -f $(BINDST)

# --- dep rules ---
gate.o: src/gate.c include/gate.h
list.o: src/list.c include/list.h
threadpool.o: src/threadpool.c include/threadpool.h include/list.h \
 include/gate.h

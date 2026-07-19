VERSION := 0.1

ifdef PS4_PAYLOAD_SDK
    include $(PS4_PAYLOAD_SDK)/toolchain/orbis.mk
else
    $(error PS4_PAYLOAD_SDK is undefined)
endif

BUILDDIR := build
SRCDIR   := src

ELF_NORMAL    := db-rebuilder-v$(VERSION).elf
ELF_INSTALLER := db-rebuilder-v$(VERSION)-installer.elf

ELF_STRIP := $(firstword $(wildcard $(PS4_PAYLOAD_SDK)/bin/orbis-llvm-strip) \
	$(wildcard $(PS4_PAYLOAD_SDK)/bin/orbis-strip))

SRCS := $(SRCDIR)/main.c \
        $(SRCDIR)/util.c \
        $(SRCDIR)/sfo.c \
        $(SRCDIR)/memvfs.c \
        $(SRCDIR)/sqlite_db.c

SQLITE_SRC := $(SRCDIR)/sqlite3.c

OBJS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
SQLITE_OBJ := $(BUILDDIR)/sqlite3.o

INSTALLER_MAIN_OBJ := $(BUILDDIR)/installer_main.o
INSTALLER_OBJS := $(OBJS:$(BUILDDIR)/main.o=$(INSTALLER_MAIN_OBJ))

PAYLOAD_BIN := $(BUILDDIR)/payload_normal_elf
PAYLOAD_OBJ := $(BUILDDIR)/payload_elf.o

COMMON_CFLAGS := -O2 -std=c11 -DPLATFORM_PS4=1 -I$(SRCDIR) \
                 -ffunction-sections -fdata-sections \
                 -fno-asynchronous-unwind-tables \
                 -DPAYLOAD_VERSION=\"$(VERSION)\" \
                 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
                 -DSQLITE_OMIT_DEPRECATED \
                 -DSQLITE_OMIT_PROGRESS_CALLBACK \
                 -DSQLITE_OMIT_SHARED_CACHE \
                 -DSQLITE_OMIT_TCL_VARIABLE \
                 -DSQLITE_OMIT_AUTHORIZATION \
                 -DSQLITE_OMIT_COMPLETE \
                 -DSQLITE_OMIT_GET_TABLE \
                 -DSQLITE_OMIT_INCRBLOB

CFLAGS := -Wall -Wextra -Werror $(COMMON_CFLAGS)

SQLITE_CFLAGS := -w $(COMMON_CFLAGS) -Os

LDLIBS := -lc -lkernel

LDFLAGS := -Wl,--gc-sections

.PHONY: all clean test

all: $(ELF_NORMAL) $(ELF_INSTALLER)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/memvfs.o: $(SRCDIR)/memvfs.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Wno-unused-parameter -c -o $@ $<

$(SQLITE_OBJ): $(SQLITE_SRC) | $(BUILDDIR)
	$(CC) $(SQLITE_CFLAGS) -c -o $@ $<

$(ELF_NORMAL): $(OBJS) $(SQLITE_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(ELF_STRIP) --strip-all $@
	$(ELF_STRIP) --remove-section=.eh_frame --remove-section=.eh_frame_hdr $@

$(INSTALLER_MAIN_OBJ): $(SRCDIR)/main.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -DBUILD_INSTALLER -c -o $@ $<

$(PAYLOAD_BIN): $(ELF_NORMAL) | $(BUILDDIR)
	cp $< $@

$(PAYLOAD_OBJ): $(PAYLOAD_BIN) | $(BUILDDIR)
	cd $(BUILDDIR) && $(LD) -r -b binary -m elf_x86_64 -o $(notdir $@) $(notdir $(PAYLOAD_BIN))

$(ELF_INSTALLER): $(INSTALLER_OBJS) $(SQLITE_OBJ) $(PAYLOAD_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(ELF_STRIP) --strip-all $@
	$(ELF_STRIP) --remove-section=.eh_frame --remove-section=.eh_frame_hdr $@

clean:
	rm -rf $(BUILDDIR) $(ELF_NORMAL) $(ELF_INSTALLER)

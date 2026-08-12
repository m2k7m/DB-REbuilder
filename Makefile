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
BIN_NORMAL    := db-rebuilder-v$(VERSION).bin

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

PAYLOAD_ELF_C := $(BUILDDIR)/db_rebuilder_elf.c
BOOTSTRAP_OBJ := $(BUILDDIR)/bootstrap-bin.o
INSTALLER_BOOTSTRAP_OBJ := $(BUILDDIR)/bootstrap-installer.o

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
                 -DSQLITE_OMIT_INCRBLOB \
                 -DSQLITE_OMIT_AUTOVACUUM \
                 -DSQLITE_OMIT_EXPLAIN \
                 -DSQLITE_OMIT_FOREIGN_KEY \
                 -DSQLITE_OMIT_LOOKASIDE \
                 -DSQLITE_OMIT_UTF16 \
                 -DSQLITE_OMIT_WAL \
                 -DSQLITE_OMIT_XFER_OPT

CFLAGS := -Wall -Wextra -Werror $(COMMON_CFLAGS)

SQLITE_CFLAGS := -w $(COMMON_CFLAGS) -Oz

LDLIBS := -lc -lkernel

LDFLAGS := -Wl,--gc-sections

.PHONY: all clean test

all: $(ELF_NORMAL) $(ELF_INSTALLER) $(BIN_NORMAL)

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

$(PAYLOAD_ELF_C): $(ELF_NORMAL) | $(BUILDDIR)
	cp $< $(BUILDDIR)/db_rebuilder.elf
	cd $(BUILDDIR) && xxd -i db_rebuilder.elf > $(notdir $(PAYLOAD_ELF_C))

$(BOOTSTRAP_OBJ): $(SRCDIR)/bootstrap-bin.c $(PAYLOAD_ELF_C) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(BUILDDIR) -c -o $@ $<

$(INSTALLER_BOOTSTRAP_OBJ): $(SRCDIR)/bootstrap-bin.c $(PAYLOAD_ELF_C) | $(BUILDDIR)
	$(CC) $(CFLAGS) -DBUILD_INSTALLER -I$(BUILDDIR) -c -o $@ $<

$(BIN_NORMAL): $(BOOTSTRAP_OBJ) $(SRCDIR)/bin_x86_64.x | $(BUILDDIR)
	$(LD) -T $(SRCDIR)/bin_x86_64.x -o $(BUILDDIR)/bootstrap-bin.elf $(BOOTSTRAP_OBJ)
	$(OBJCOPY) -O binary --only-section=.text $(BUILDDIR)/bootstrap-bin.elf $@

$(ELF_INSTALLER): $(INSTALLER_BOOTSTRAP_OBJ) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(ELF_STRIP) --strip-all $@
	$(ELF_STRIP) --remove-section=.eh_frame --remove-section=.eh_frame_hdr $@

clean:
	rm -rf $(BUILDDIR) $(ELF_NORMAL) $(ELF_INSTALLER) $(BIN_NORMAL)

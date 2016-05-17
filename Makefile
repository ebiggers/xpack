#
# Define V=1 to enable "verbose" mode, showing all executed commands.
#
# Define DECOMPRESSION_ONLY=yes to omit all compression code, building a
# decompression-only library.  If doing this, you must also build a specific
# library target such as 'libxpack.a', as the programs will no longer compile.
#
# TODO: ENABLE_PREPROCESSING option
#

CC ?= gcc
AR ?= ar

STATIC_LIB_SUFFIX := .a
SHARED_LIB_SUFFIX := .so
PROG_SUFFIX       :=
PROG_CFLAGS       :=
PIC_REQUIRED      := yes
HARD_LINKS        := yes

# Compiling for Windows with MinGW?
ifneq ($(findstring -mingw,$(CC)),)
    ifeq ($(AR),ar)
        AR := $(patsubst %-gcc,%-ar,$(CC))
    endif
    STATIC_LIB_SUFFIX := .lib
    SHARED_LIB_SUFFIX := .dll
    PROG_SUFFIX       := .exe
    PROG_CFLAGS       := -static -municode
    PIC_REQUIRED      := no
    HARD_LINKS        := no
endif

##############################################################################

#### Common compiler flags; not intended to be overridden

override CFLAGS += -O2 -fomit-frame-pointer -std=gnu89 -I. -Icommon	\
		-Wall -Wundef -Wdeclaration-after-statement		\
		-Wmissing-prototypes -Wstrict-prototypes

##############################################################################

#### Quiet make is enabled by default.  Define V=1 to disable.

ifneq ($(findstring s,$(MAKEFLAGS)),s)
ifneq ($(V),1)
        QUIET_CC       = @echo '  CC      ' $@;
        QUIET_CCLD     = @echo '  CCLD    ' $@;
        QUIET_AR       = @echo '  AR      ' $@;
        QUIET_LN       = @echo '  LN      ' $@;
        QUIET_CP       = @echo '  CP      ' $@;
        QUIET_GEN      = @echo '  GEN     ' $@;
endif
endif

##############################################################################

COMMON_HEADERS := $(wildcard common/*.h)
ALL_TARGETS :=

#### Library

STATIC_LIB := libxpack$(STATIC_LIB_SUFFIX)
SHARED_LIB := libxpack$(SHARED_LIB_SUFFIX)

LIB_CFLAGS += $(CFLAGS) -fvisibility=hidden -D_ANSI_SOURCE

DECOMPRESSION_ONLY := no
ifeq ($(DECOMPRESSION_ONLY),yes)
    LIB_CFLAGS += -DDECOMPRESSION_ONLY=1
endif

ENABLE_PREPROCESSING := no
ifeq ($(ENABLE_PREPROCESSING),yes)
    LIB_CFLAGS += -DENABLE_PREPROCESSING=1
endif

LIB_HEADERS := $(wildcard lib/*.h)

LIB_SRC := lib/x86_cpu_features.c	\
	   lib/xpack_common.c		\
	   lib/xpack_compress.c		\
	   lib/xpack_decompress.c

LIB_OBJ := $(LIB_SRC:.c=.o)
LIB_PIC_OBJ := $(LIB_SRC:.c=.pic.o)
ifeq ($(PIC_REQUIRED),yes)
    SHLIB_OBJ := $(LIB_PIC_OBJ)
else
    SHLIB_OBJ := $(LIB_OBJ)
endif

# Compile position dependent library object files
$(LIB_OBJ): %.o: %.c $(LIB_HEADERS) $(COMMON_HEADERS) .lib-cflags
	$(QUIET_CC) $(CC) -o $@ -c $(LIB_CFLAGS) $<

# Compile position independent library object files
$(LIB_PIC_OBJ): %.pic.o: %.c $(LIB_HEADERS) $(COMMON_HEADERS) .lib-cflags
	$(QUIET_CC) $(CC) -o $@ -c $(LIB_CFLAGS) -fPIC $<

# Link shared library
$(SHARED_LIB):$(SHLIB_OBJ)
	$(QUIET_CCLD) $(CC) -o $@ $(LDFLAGS) $(LIB_CFLAGS) -shared $+

ALL_TARGETS += $(SHARED_LIB)

# Create static library
$(STATIC_LIB):$(LIB_OBJ)
	$(QUIET_AR) $(AR) cr $@ $+

ALL_TARGETS += $(STATIC_LIB)

# Rebuild if CC or LIB_CFLAGS changed
.lib-cflags: FORCE
	@flags='$(CC):$(LIB_CFLAGS)'; \
	if [ "$$flags" != "`cat $@ 2>/dev/null`" ]; then \
		[ -e $@ ] && echo "Rebuilding library due to new compiler flags"; \
		echo "$$flags" > $@; \
	fi

##############################################################################

#### Programs

PROG_CFLAGS += $(CFLAGS)		\
	       -D_DEFAULT_SOURCE	\
	       -D_FILE_OFFSET_BITS=64	\
	       -DHAVE_CONFIG_H

PROG_COMMON_HEADERS := programs/prog_util.h programs/config.h
PROG_COMMON_SRC := programs/prog_util.c programs/tgetopt.c
PROG_SPECIFIC_SRC := programs/xpack.c programs/benchmark.c

PROG_COMMON_OBJ := $(PROG_COMMON_SRC:.c=.o)
PROG_SPECIFIC_OBJ := $(PROG_SPECIFIC_SRC:.c=.o)
PROG_OBJ := $(PROG_COMMON_OBJ) $(PROG_SPECIFIC_OBJ)

# Generate autodetected configuration header
programs/config.h:programs/detect.sh .prog-cflags
	$(QUIET_GEN) CC=$(CC) $< > $@

# Compile program object files
$(PROG_OBJ): %.o: %.c $(PROG_COMMON_HEADERS) $(COMMON_HEADERS) .prog-cflags
	$(QUIET_CC) $(CC) -o $@ -c $(PROG_CFLAGS) $<

# Link benchmark program
benchmark$(PROG_SUFFIX):programs/benchmark.o $(PROG_COMMON_OBJ) $(STATIC_LIB)
	$(QUIET_CCLD) $(CC) -o $@ $(LDFLAGS) $(PROG_CFLAGS) $+

ALL_TARGETS += benchmark$(PROG_SUFFIX)

# Link xpack program
xpack$(PROG_SUFFIX):programs/xpack.o $(PROG_COMMON_OBJ) $(STATIC_LIB)
	$(QUIET_CCLD) $(CC) -o $@ $(LDFLAGS) $(PROG_CFLAGS) $+

ALL_TARGETS += xpack$(PROG_SUFFIX)

ifeq ($(HARD_LINKS),yes)
# Hard link xunpack to xpack
xunpack$(PROG_SUFFIX):xpack$(PROG_SUFFIX)
	$(QUIET_LN) ln -f $< $@
else
# No hard links; copy xpack to xunpack
xunpack$(PROG_SUFFIX):xpack$(PROG_SUFFIX)
	$(QUIET_CP) cp -f $< $@
endif

ALL_TARGETS += xunpack$(PROG_SUFFIX)

# Rebuild if CC or PROG_CFLAGS changed
.prog-cflags: FORCE
	@flags='$(CC):$(PROG_CFLAGS)'; \
	if [ "$$flags" != "`cat $@ 2>/dev/null`" ]; then \
		[ -e $@ ] && echo "Rebuilding programs due to new compiler flags"; \
		echo "$$flags" > $@; \
	fi

##############################################################################

all:$(ALL_TARGETS)

help:
	@echo "Available targets:"
	@echo "------------------"
	@for target in $(ALL_TARGETS); do	\
		echo -e "$$target";		\
	done

clean:
	rm -f *.a *.dll *.exe *.exp *.lib *.so \
		lib/*.o lib/*.obj programs/*.o programs/*.obj \
		benchmark xpack xunpack programs/config.h \
		.lib-cflags .prog-cflags

realclean: clean
	rm -f tags cscope*

FORCE:

.PHONY: all help clean realclean

.DEFAULT_GOAL = all

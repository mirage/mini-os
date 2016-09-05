#
# Compare $(1) and $(2) and replace $(2) with $(1) if they differ
#
# Typically $(1) is a newly generated file and $(2) is the target file
# being regenerated. This prevents changing the timestamp of $(2) only
# due to being auto regenereated with the same contents.
define move-if-changed
        if ! cmp -s $(1) $(2); then mv -f $(1) $(2); else rm -f $(1); fi
endef

# cc-option: Check if compiler supports first option, else fall back to second.
#
# This is complicated by the fact that unrecognised -Wno-* options:
#   (a) are ignored unless the compilation emits a warning; and
#   (b) even then produce a warning rather than an error
# To handle this we do a test compile, passing the option-under-test, on a code
# fragment that will always produce a warning (integer assigned to pointer).
# We then grep for the option-under-test in the compiler's output, the presence
# of which would indicate an "unrecognized command-line option" warning/error.
#
# Usage: cflags-y += $(call cc-option,$(CC),-march=winchip-c6,-march=i586)
cc-option = $(shell if test -z "`echo 'void*p=1;' | \
              $(1) $(2) -S -o /dev/null -x c - 2>&1 | grep -- $(2) -`"; \
              then echo "$(2)"; else echo "$(3)"; fi ;)

ifneq ($(MINIOS_CONFIG),)
EXTRA_DEPS += $(MINIOS_CONFIG)
include $(MINIOS_CONFIG)
endif

# Compatibility with Xen's stubdom build environment.  If we are building
# stubdom, some XEN_ variables are set, set MINIOS_ variables accordingly.
#
ifneq ($(XEN_ROOT),)
MINIOS_ROOT=$(XEN_ROOT)/extras/mini-os
else
MINIOS_ROOT=$(TOPLEVEL_DIR)
endif
export MINIOS_ROOT

ifneq ($(XEN_TARGET_ARCH),)
MINIOS_TARGET_ARCH = $(XEN_TARGET_ARCH)
else
MINIOS_COMPILE_ARCH    ?= $(shell uname -m | sed -e s/i.86/x86_32/ \
                            -e s/i86pc/x86_32/ -e s/amd64/x86_64/ \
                            -e s/armv7.*/arm32/ -e s/armv8.*/arm64/ \
                            -e s/aarch64/arm64/)

MINIOS_TARGET_ARCH     ?= $(MINIOS_COMPILE_ARCH)
endif

libc = $(stubdom)

XEN_INTERFACE_VERSION ?= 0x00030205
export XEN_INTERFACE_VERSION

# Try to find out the architecture family TARGET_ARCH_FAM.
# First check whether x86_... is contained (for x86_32, x86_32y, x86_64).
# If not x86 then use $(MINIOS_TARGET_ARCH)
ifeq ($(findstring x86_,$(MINIOS_TARGET_ARCH)),x86_)
TARGET_ARCH_FAM = x86
else
TARGET_ARCH_FAM = $(MINIOS_TARGET_ARCH)
endif

# The architecture family directory below mini-os.
TARGET_ARCH_DIR := arch/$(TARGET_ARCH_FAM)

# Export these variables for possible use in architecture dependent makefiles.
export TARGET_ARCH_DIR
export TARGET_ARCH_FAM

# This is used for architecture specific links.
# This can be overwritten from arch specific rules.
ARCH_LINKS =

# The path pointing to the architecture specific header files.
ARCH_INC := $(TARGET_ARCH_FAM)

# For possible special header directories.
# This can be overwritten from arch specific rules.
EXTRA_INC = $(ARCH_INC)	

# Include the architecture family's special makerules.
# This must be before include minios.mk!
include $(MINIOS_ROOT)/$(TARGET_ARCH_DIR)/arch.mk

extra_incl := $(foreach dir,$(EXTRA_INC),-isystem $(MINIOS_ROOT)/include/$(dir))

DEF_CPPFLAGS += -isystem $(MINIOS_ROOT)/include
DEF_CPPFLAGS += -D__MINIOS__

ifeq ($(libc),y)
DEF_CPPFLAGS += -DHAVE_LIBC
DEF_CPPFLAGS += -isystem $(MINIOS_ROOT)/include/posix
DEF_CPPFLAGS += -isystem $(XEN_ROOT)/tools/xenstore/include
endif

ifneq ($(LWIPDIR),)
lwip=y
DEF_CPPFLAGS += -DHAVE_LWIP
DEF_CPPFLAGS += -isystem $(LWIPDIR)/src/include
DEF_CPPFLAGS += -isystem $(LWIPDIR)/src/include/ipv4
endif

# Set tools
AS         = $(CROSS_COMPILE)as
LD         = $(CROSS_COMPILE)ld
ifeq ($(clang),y)
CC         = $(CROSS_COMPILE)clang
LD_LTO     = $(CROSS_COMPILE)llvm-ld
else
CC         = $(CROSS_COMPILE)gcc
LD_LTO     = $(CROSS_COMPILE)ld
endif
CPP        = $(CC) -E
AR         = $(CROSS_COMPILE)ar
RANLIB     = $(CROSS_COMPILE)ranlib
NM         = $(CROSS_COMPILE)nm
STRIP      = $(CROSS_COMPILE)strip
OBJCOPY    = $(CROSS_COMPILE)objcopy
OBJDUMP    = $(CROSS_COMPILE)objdump
SIZEUTIL   = $(CROSS_COMPILE)size

# Allow git to be wrappered in the environment
GIT        ?= git

INSTALL      = install
INSTALL_DIR  = $(INSTALL) -d -m0755 -p
INSTALL_DATA = $(INSTALL) -m0644 -p
INSTALL_PROG = $(INSTALL) -m0755 -p

BOOT_DIR ?= /boot

SOCKET_LIBS =
UTIL_LIBS = -lutil
DLOPEN_LIBS = -ldl

SONAME_LDFLAG = -soname
SHLIB_LDFLAGS = -shared

ifneq ($(debug),y)
CFLAGS += -O2 -fomit-frame-pointer
else
# Less than -O1 produces bad code and large stack frames
CFLAGS += -O1 -fno-omit-frame-pointer
CFLAGS-$(gcc) += -fno-optimize-sibling-calls
endif

ifeq ($(lto),y)
CFLAGS += -flto
LDFLAGS-$(clang) += -plugin LLVMgold.so
endif

# When adding a new CONFIG_ option please make sure the test configurations
# under arch/*/testbuild/ are updated accordingly. Especially
# arch/*/testbuild/*-yes and arch/*/testbuild/*-no should set ALL possible
# CONFIG_ variables.

# Configuration defaults
ifeq ($(TARGET_ARCH_FAM),x86)
CONFIG_PARAVIRT ?= y
else
CONFIG_PARAVIRT ?= n
endif
CONFIG_START_NETWORK ?= y
CONFIG_SPARSE_BSS ?= y
CONFIG_QEMU_XS_ARGS ?= n
CONFIG_TEST ?= n
CONFIG_PCIFRONT ?= n
CONFIG_BLKFRONT ?= y
CONFIG_TPMFRONT ?= n
CONFIG_TPM_TIS ?= n
CONFIG_TPMBACK ?= n
CONFIG_NETFRONT ?= y
CONFIG_FBFRONT ?= y
CONFIG_KBDFRONT ?= y
CONFIG_CONSFRONT ?= y
CONFIG_XENBUS ?= y
CONFIG_XC ?=y
CONFIG_LWIP ?= $(lwip)
CONFIG_BALLOON ?= n

# Export config items as compiler directives
DEFINES-$(CONFIG_PARAVIRT) += -DCONFIG_PARAVIRT
DEFINES-$(CONFIG_START_NETWORK) += -DCONFIG_START_NETWORK
DEFINES-$(CONFIG_SPARSE_BSS) += -DCONFIG_SPARSE_BSS
DEFINES-$(CONFIG_QEMU_XS_ARGS) += -DCONFIG_QEMU_XS_ARGS
DEFINES-$(CONFIG_PCIFRONT) += -DCONFIG_PCIFRONT
DEFINES-$(CONFIG_BLKFRONT) += -DCONFIG_BLKFRONT
DEFINES-$(CONFIG_TPMFRONT) += -DCONFIG_TPMFRONT
DEFINES-$(CONFIG_TPM_TIS) += -DCONFIG_TPM_TIS
DEFINES-$(CONFIG_TPMBACK) += -DCONFIG_TPMBACK
DEFINES-$(CONFIG_NETFRONT) += -DCONFIG_NETFRONT
DEFINES-$(CONFIG_KBDFRONT) += -DCONFIG_KBDFRONT
DEFINES-$(CONFIG_FBFRONT) += -DCONFIG_FBFRONT
DEFINES-$(CONFIG_CONSFRONT) += -DCONFIG_CONSFRONT
DEFINES-$(CONFIG_XENBUS) += -DCONFIG_XENBUS
DEFINES-$(CONFIG_BALLOON) += -DCONFIG_BALLOON

DEFINES-y += -D__XEN_INTERFACE_VERSION__=$(XEN_INTERFACE_VERSION)

# Override settings for this OS
PTHREAD_LIBS =
nosharedlibs=y

# Common Makefile for mini-os.
#
# Every architecture directory below mini-os/arch has to have a
# Makefile and a arch.mk.
#

OBJ_DIR=$(CURDIR)
TOPLEVEL_DIR=$(CURDIR)

include Config.mk

# Symlinks and headers that must be created before building the C files
GENERATED_HEADERS := include/list.h $(ARCH_LINKS) include/mini-os include/$(TARGET_ARCH_FAM)/mini-os

EXTRA_DEPS += $(GENERATED_HEADERS)

# Include common mini-os makerules.
include minios.mk

# Set tester flags
# CFLAGS += -DBLKTEST_WRITE

# Define some default flags for linking.
LDLIBS := 
APP_LDLIBS := 
LDARCHLIB := -L$(OBJ_DIR)/$(TARGET_ARCH_DIR) -l$(ARCH_LIB_NAME)
LDFLAGS_FINAL := -T $(OBJ_DIR)/$(TARGET_ARCH_DIR)/minios-$(MINIOS_TARGET_ARCH).lds $(ARCH_LDFLAGS_FINAL)

# Prefix for global API names. All other symbols are localised before
# linking with EXTRA_OBJS.
GLOBAL_PREFIX := xenos_
EXTRA_OBJS =

TARGET := mini-os

# Subdirectories common to mini-os
SUBDIRS := lib xenbus console

src-$(CONFIG_BLKFRONT) += blkfront.c
src-$(CONFIG_TPMFRONT) += tpmfront.c
src-$(CONFIG_TPM_TIS) += tpm_tis.c
src-$(CONFIG_TPMBACK) += tpmback.c
src-y += daytime.c
src-y += events.c
src-$(CONFIG_FBFRONT) += fbfront.c
src-y += gntmap.c
src-y += gnttab.c
src-y += hypervisor.c
src-y += kernel.c
src-y += lock.c
src-y += main.c
src-y += mm.c
src-$(CONFIG_NETFRONT) += netfront.c
src-$(CONFIG_PCIFRONT) += pcifront.c
src-y += sched.c
src-$(CONFIG_TEST) += test.c
src-$(CONFIG_BALLOON) += balloon.c

src-y += lib/ctype.c
src-y += lib/math.c
src-y += lib/printf.c
src-y += lib/stack_chk_fail.c
src-y += lib/string.c
src-y += lib/sys.c
src-y += lib/xmalloc.c
src-$(CONFIG_XENBUS) += lib/xs.c

src-$(CONFIG_XENBUS) += xenbus/xenbus.c

src-y += console/console.c
src-y += console/xencons_ring.c
src-$(CONFIG_CONSFRONT) += console/xenbus.c

# The common mini-os objects to build.
APP_OBJS :=
OBJS := $(patsubst %.c,$(OBJ_DIR)/%.o,$(src-y))

.PHONY: default
default: $(OBJ_DIR)/$(TARGET)

# Create special architecture specific links. The function arch_links
# has to be defined in arch.mk (see include above).
ifneq ($(ARCH_LINKS),)
$(ARCH_LINKS):
	$(arch_links)
endif

include/list.h: include/minios-external/bsd-sys-queue-h-seddery include/minios-external/bsd-sys-queue.h
	perl $^ --prefix=minios  >$@.new
	$(call move-if-changed,$@.new,$@)

# Used by stubdom's Makefile
.PHONY: links
links: $(GENERATED_HEADERS)

include/mini-os:
	ln -sf . $@

include/$(TARGET_ARCH_FAM)/mini-os:
	ln -sf . $@

.PHONY: arch_lib
arch_lib:
	$(MAKE) --directory=$(TARGET_ARCH_DIR) OBJ_DIR=$(OBJ_DIR)/$(TARGET_ARCH_DIR) || exit 1;

ifeq ($(CONFIG_LWIP),y)
# lwIP library
LWC	:= $(sort $(shell find $(LWIPDIR)/src -type f -name '*.c'))
LWC	:= $(filter-out %6.c %ip6_addr.c %ethernetif.c, $(LWC))
LWO	:= $(patsubst %.c,%.o,$(LWC))
LWO	+= $(OBJ_DIR)/lwip-arch.o
ifeq ($(CONFIG_NETFRONT),y)
LWO += $(OBJ_DIR)/lwip-net.o
endif

$(OBJ_DIR)/lwip.a: $(LWO)
	$(RM) $@
	$(AR) cqs $@ $^

OBJS += $(OBJ_DIR)/lwip.a
endif

OBJS := $(filter-out $(OBJ_DIR)/lwip%.o $(LWO), $(OBJS))

ifeq ($(libc),y)
ifeq ($(CONFIG_XC),y)
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/toolcore -whole-archive -lxentoolcore -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/toolcore/libxentoolcore.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/toollog -whole-archive -lxentoollog -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/toollog/libxentoollog.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/evtchn -whole-archive -lxenevtchn -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/evtchn/libxenevtchn.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/gnttab -whole-archive -lxengnttab -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/gnttab/libxengnttab.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/call -whole-archive -lxencall -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/call/libxencall.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/foreignmemory -whole-archive -lxenforeignmemory -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/foreignmemory/libxenforeignmemory.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/devicemodel -whole-archive -lxendevicemodel -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libs-$(MINIOS_TARGET_ARCH)/devicemodel/libxendevicemodel.a
APP_LDLIBS += -L$(XEN_ROOT)/stubdom/libxc-$(MINIOS_TARGET_ARCH) -whole-archive -lxenguest -lxenctrl -no-whole-archive
LIBS += $(XEN_ROOT)/stubdom/libxc-$(MINIOS_TARGET_ARCH)/libxenctrl.a
LIBS += $(XEN_ROOT)/stubdom/libxc-$(MINIOS_TARGET_ARCH)/libxenguest.a
endif
APP_LDLIBS += -lpci
APP_LDLIBS += -lz
APP_LDLIBS += -lm
LDLIBS += -lc
endif

ifneq ($(APP_OBJS)-$(lwip),-y)
OBJS := $(filter-out $(OBJ_DIR)/daytime.o, $(OBJS))
endif

$(OBJ_DIR)/$(TARGET)_app.o: $(APP_OBJS) app.lds $(LIBS)
	$(LD) -r -d $(LDFLAGS) -\( $(APP_OBJS) -T app.lds -\) $(APP_LDLIBS) --undefined main -o $@

ifneq ($(APP_OBJS),)
APP_O=$(OBJ_DIR)/$(TARGET)_app.o 
endif

# Special rule for x86 for now
$(OBJ_DIR)/arch/x86/minios-x86%.lds:  arch/x86/minios-x86.lds.S
	$(CPP) $(ASFLAGS) -P $< -o $@

$(OBJ_DIR)/$(TARGET): $(OBJS) $(APP_O) arch_lib $(OBJ_DIR)/$(TARGET_ARCH_DIR)/minios-$(MINIOS_TARGET_ARCH).lds
	$(LD) -r $(LDFLAGS) $(HEAD_OBJ) $(APP_O) $(OBJS) $(LDARCHLIB) $(LDLIBS) -o $@.o
	$(OBJCOPY) -w -G $(GLOBAL_PREFIX)* -G _start $@.o $@.o
	$(LD) $(LDFLAGS) $(LDFLAGS_FINAL) $@.o $(EXTRA_OBJS) -o $@
	gzip -n -f -9 -c $@ >$@.gz

.PHONY: config
CONFIG_FILE ?= $(CURDIR)/minios-config.mk
config:
	echo "$(DEFINES-y)" >$(CONFIG_FILE)

.PHONY: clean arch_clean

arch_clean:
	$(MAKE) --directory=$(TARGET_ARCH_DIR) OBJ_DIR=$(OBJ_DIR)/$(TARGET_ARCH_DIR) clean || exit 1;

clean:	arch_clean
	for dir in $(addprefix $(OBJ_DIR)/,$(SUBDIRS)); do \
		rm -f $$dir/*.o; \
	done
	rm -f include/list.h
	rm -f $(OBJ_DIR)/*.o *~ $(OBJ_DIR)/core $(OBJ_DIR)/$(TARGET).elf $(OBJ_DIR)/$(TARGET).raw $(OBJ_DIR)/$(TARGET) $(OBJ_DIR)/$(TARGET).gz
	find . $(OBJ_DIR) -type l | xargs rm -f
	$(RM) $(OBJ_DIR)/lwip.a $(LWO)
	rm -f tags TAGS

.PHONY: testbuild
TEST_CONFIGS := $(wildcard $(CURDIR)/$(TARGET_ARCH_DIR)/testbuild/*)
testbuild:
	for arch in $(MINIOS_TARGET_ARCHS); do \
		for conf in $(TEST_CONFIGS); do \
			$(MAKE) clean; \
			MINIOS_TARGET_ARCH=$$arch MINIOS_CONFIG=$$conf $(MAKE) || exit 1; \
		done; \
	done
	$(MAKE) clean

define all_sources
     ( find . -name '*.[chS]' -print )
endef

.PHONY: cscope
cscope:
	$(all_sources) > cscope.files
	cscope -k -b -q
    
.PHONY: tags
tags:
	$(all_sources) | xargs ctags

.PHONY: TAGS
TAGS:
	$(all_sources) | xargs etags

.PHONY: gtags
gtags:
	$(all_sources) | gtags -f -

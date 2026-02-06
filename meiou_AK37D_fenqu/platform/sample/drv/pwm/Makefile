# Project name

PROJ = ak_drv_pwm_sample

LIB_DEP = plat_common plat_log plat_mem plat_thread plat_osal plat_dbg
LIB_DEP += pthread dl

LIBS = $(addprefix -l,$(LIB_DEP))

ifeq ($(OS_TYPE), 1)
    TARGET = lib$(PROJ).a
else
    TARGET = $(PROJ)
endif

TOPDIR 	?= $(shell pwd)/../../..

# Flags

INCLUDE    =	-I. \
		-I../../../include_inner \
		-I../../../include \

#Locations

SRCDIR += .

CSRCS += $(foreach d, $(SRCDIR), $(wildcard $d/*.c))

COBJS += $(patsubst %.c, %.o, $(CSRCS))

LIB_DIR = $(TOPDIR)/lib

LD_FLAGS = -Xlinker "-(" $(LIBS) -Xlinker "-)" -L$(LIB_DIR)

#build rule
.PHONY: all target install clean

all: $(COBJS)
ifeq ($(OS_TYPE), 1)
	$(AR) -rsv $(TARGET) $(COBJS)
	@cp -r $(TARGET) $(TOPDIR)/lib
else
	$(CC) $(COBJS) $(LD_FLAGS) -o $(TARGET)
endif

install:
ifeq ($(OS_TYPE), 0)
	@cp -f $(TARGET) $(SAMPLE_INSTALL_DIR)
else
	@cp -f $(TARGET) $(PLATFORM_INSTALL_DIR)/lib
endif

clean : 
	$(RM) $(COBJS) $(TARGET)

ifndef RULES_MK_FILE
RULES_MK_FILE=$(TOPDIR)/../../rules.mk
endif


# Rules
include $(RULES_MK_FILE)

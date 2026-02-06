# Project name

PROJ = ak_video_sample

LIBS = $(addprefix -l, $(LIB_DEP))

TARGET = ak_video_sample

ifneq ($(OS_TYPE), 0)
    TARGET = libak_video_sample.a
    LIB_DEP = plat_common  plat_mem plat_thread pthread  plat_log plat_osal mpi_venc akv_encode plat_dbg app_video plat_vi
else
    TARGET = ak_video_sample
    LIB_DEP = plat_common  plat_mem plat_thread pthread  plat_log plat_osal mpi_venc akv_encode plat_dbg app_video plat_vi

ifneq ($(CHIP_SERIES), AK37E)
    LIB_DEP += plat_isp_sdk plat_vpss
endif
endif

TOPDIR 	?= $(shell pwd)/../..

# Flags

INCLUDE    =	-I. \
		-I../../include_inner \
		-I../../include \
		-I$(CORE_MODULE_DIR)/include \
		-I$(CORE_MODULE_DIR)/include_inner \

#Locations

SRCDIR += .

CSRCS += $(foreach d, $(SRCDIR), $(wildcard $d/*.c))

COBJS += $(patsubst %.c, %.o, $(CSRCS))

LIB_DIR = $(TOPDIR)/lib
CORE_MODULE_LIB_DIR = $(CORE_MODULE_DIR)/lib

LD_FLAGS = -Xlinker "-(" $(LIBS) -Xlinker "-)" -L$(LIB_DIR) -L$(CORE_MODULE_LIB_DIR) -L$(EXT_LIB_DIR)

#build rule
.PHONY: all target install clean

all: $(COBJS)
	$(CC) $(COBJS) $(LD_FLAGS) -o $(TARGET)
	$(STRIP) $(TARGET)

install:
	@cp -f $(TARGET) $(SAMPLE_INSTALL_DIR)

clean : 
	$(RM) $(COBJS) $(TARGET)


# Rules
include $(RULES_MK_FILE)

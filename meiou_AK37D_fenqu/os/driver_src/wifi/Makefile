dir_y :=
dir_y += rtl8188FU_linux_v5.7.4_33085.20190419
dir_y += rtl8188EUS_linux_v5.7.6.1_35670.20191106
dir_y += rtl8189FS_linux_v5.7.9_35795.20191128
dir_y += atbm603X_wifi_Sv1997_Lv13216_20211201
#dir_y += L.SMAC.19Q3.2053.01
dir_y +=  L.SMAC.19Q3.2126.02_linux_1012

dir_y +=  rtl8733BU_WiFi_linux_v5.15.12-126-gb999e6008.20240911_COEX20230616-330e

dir_y +=  Hi3881V100R001C00SPC021B006
INSTALL := 

# for L.SMAC.19Q3.2053.01  ssv6155p
#KBUILD_TOP := $(shell pwd)/L.SMAC.19Q3.2053.01
#SSV_DRV_PATH := $(shell pwd)/L.SMAC.19Q3.2053.01

KBUILD_TOP := $(shell pwd)/L.SMAC.19Q3.2126.02_linux_1012
SSV_DRV_PATH := $(shell pwd)/L.SMAC.19Q3.2126.02_linux_1012
# end for L.SMAC.19Q3.2053.01  ssv6155p 
export KBUILD_TOP
export SSV_DRV_PATH

export KSRC
export INSTALL

AUTOCONF := $(KSRC)/include/config/auto.conf
FEXIST := $(shell if [ -f $(AUTOCONF) ]; then echo "exist"; else echo "noexist"; fi;)
ifeq ("$(FEXIST)", "exist")
SOURCES = $(AUTOCONF)
include $(SOURCES)
endif 


all:
	@for i in $(dir_y); \
	do \
		$(MAKE) -C $$i; \
		if [ $$? -ne 0 ];then exit 1; fi \
	done

strip:
	@for i in $(dir_y); \
	do \
		$(MAKE) -C $$i strip; \
	done
clean:
	@for i in $(dir_y); \
	do \
		$(MAKE) -C $$i clean; \
	done

install:
	@rm -rf $(INSTALL)
	@mkdir $(INSTALL)
	@find . -name *.ko | xargs -i cp {} $(INSTALL)

help:
	@echo "Compile :"
	@echo "    --KSRC : Kernel Build Directory(Only Support Absolute Path)"
	@echo "    --examples : "
	@echo "           make KSRC=/home/test/linux/bd"
	@echo "Install :"
	@echo "    --INSTALL : Modules Intall Directory(Only Support Absolute Path)"
	@echo "    --examples : "
	@echo "           make install INSTALL=/home/test/linux/modules"

.PHONY: all clean help

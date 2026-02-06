dir_y :=
dir_y += c2399
dir_y += c3390
dir_y += c4390
dir_y += f22_f23_f28_f35_f37
dir_y += h63
dir_y += mis2006
dir_y += mis2008
dir_y += mis5001
dir_y += sc2145
dir_y += sc2231
dir_y += sc2239
dir_y += sc223a
dir_y += sc2332
dir_y += sc2335
dir_y += sc2336
dir_y += sc3235
dir_y += sc3335
dir_y += sc4236
dir_y += sc401ai
dir_y += sc501ai
dir_y += sc5238
dir_y += sc5335p
dir_y += os02g10
dir_y += pr2000
dir_y += ps5268
dir_y += q03
dir_y += ar0230
dir_y += gc2063
dir_y += gc4653
dir_y += gc0308
dir_y += tp9950

INSTALL :=
#KSRC := $(shell pwd)/../bd
export KSRC
export INSTALL

all:
	cp -rf ../include/ak_isp_drv.h ./include/
	cp -rf ../include/ak_isp_char.h ./include/
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

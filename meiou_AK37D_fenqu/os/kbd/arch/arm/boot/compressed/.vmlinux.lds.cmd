cmd_arch/arm/boot/compressed/vmlinux.lds := /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/tools/arm-anykav500-linux-uclibcgnueabi/usr/bin/arm-anykav500-linux-uclibcgnueabi-gcc -E -Wp,-MD,arch/arm/boot/compressed/.vmlinux.lds.d  -nostdinc -isystem /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/tools/arm-anykav500-linux-uclibcgnueabi/bin/../lib/gcc/arm-anykav500-linux-uclibcgnueabi/4.9.4/include -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/include -Iarch/arm/include/generated/uapi -Iarch/arm/include/generated  -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/include -Iinclude -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/include/uapi -Iarch/arm/include/generated/uapi -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/include/uapi -Iinclude/generated/uapi -include /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/include/linux/kconfig.h -D__KERNEL__ -mlittle-endian   -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/mach-anycloud/include -DTEXT_START="0" -DBSS_START="ALIGN(8)" -P -C -Uarm -D__ASSEMBLY__ -DLINKER_SCRIPT -o arch/arm/boot/compressed/vmlinux.lds /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/compressed/vmlinux.lds.S

source_arch/arm/boot/compressed/vmlinux.lds := /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/compressed/vmlinux.lds.S

deps_arch/arm/boot/compressed/vmlinux.lds := \
    $(wildcard include/config/cpu/endian/be8.h) \

arch/arm/boot/compressed/vmlinux.lds: $(deps_arch/arm/boot/compressed/vmlinux.lds)

$(deps_arch/arm/boot/compressed/vmlinux.lds):

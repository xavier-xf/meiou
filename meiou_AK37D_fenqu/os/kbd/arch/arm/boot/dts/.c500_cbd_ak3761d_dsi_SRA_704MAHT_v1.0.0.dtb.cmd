cmd_arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb := mkdir -p arch/arm/boot/dts/ ; /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/tools/arm-anykav500-linux-uclibcgnueabi/usr/bin/arm-anykav500-linux-uclibcgnueabi-gcc -E -Wp,-MD,arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.d.pre.tmp -nostdinc -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/include -I/home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/drivers/of/testcase-data -undef -D__DTS__ -x assembler-with-cpp -o arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.dts.tmp /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dts ; ./scripts/dtc/dtc -O dtb -o arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb -b 0 -i /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/  -d arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.d.dtc.tmp arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.dts.tmp ; cat arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.d.pre.tmp arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.d.dtc.tmp > arch/arm/boot/dts/.c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb.d

source_arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb := /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dts

deps_arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb := \
  /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/anycloud_ak37d1.dtsi \
  /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/anycloud_ak37d_common.dtsi \
  /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/include/dt-bindings/gpio/gpio.h \
  /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/anycloud_ak37d_lcd.dtsi \
  /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/anycloud_ak37d_norflash.dtsi \
  /home/xiaoxiao/workspace/meiou/meiou_AK37D_fenqu/os/kernel/arch/arm/boot/dts/anycloud_ak37d_nandflash1.dtsi \

arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb: $(deps_arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb)

$(deps_arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb):

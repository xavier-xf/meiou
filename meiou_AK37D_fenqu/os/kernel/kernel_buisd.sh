CROSS_COMPILE="../../tools/arm-anykav500-linux-uclibcgnueabi/bin/arm-anykav500-linux-uclibcgnueabi-"

mkdir ../kbd
make O=../kbd/ anycloud_ak37d_cdv1004qt_defconfig CROSS_COMPILE=$CROSS_COMPILE
make O=../kbd/ dtbs  modules uImage -j8 CROSS_COMPILE=$CROSS_COMPILE
#!/bin/sh

# upgrade_bin_version=$(date +"%Y%m%d%H%M%S")
# export upgrade_bin_version

# 一键制作分区表
# rm env.img
# tr '\000' '\377' < /dev/zero | dd of=./env.img bs=1024 count=4
# ./fw_setenv -s env_ak3761d_nor.cfg
# ./fw_printenv


rm env.img
cp env_ak3761d_nor.cfg env_ak3761d_nor_tmp.cfg
echo "uprade_image_version $upgrade_bin_version" >>env_ak3761d_nor_tmp.cfg
tr '\000' '\377' </dev/zero | dd of=./env.img bs=1024 count=4
./fw_setenv -s env_ak3761d_nor_tmp.cfg
./fw_printenv
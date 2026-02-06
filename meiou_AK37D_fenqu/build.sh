
#!/bin/bash


KERNEL_CHIP_TYPE=anycloud_ak37d_SRA_704MAHT_defconfig
UBOOT_CHIP_TYPE=anycloud_ak37d_config

# 工具链
SDK=`cd $(dirname $0);pwd -P`
CROSS_COMPILE=$SDK/tools/arm-anykav500-linux-uclibcgnueabi/usr/bin/arm-anykav500-linux-uclibcgnueabi-




# burntool file path
BURNTOOL=tools/burntool-704MAHT/platform

# 内核路径
KERNEL=$SDK/os/kernel
UBOOT=$SDK/os/uboot

# 用于编译生成的文件夹
kernel_build_dir=$SDK/os/kbd
uboot_build_dir=$SDK/os/ubd

# Compile the generated file
uImage=os/kbd/arch/arm/boot/uImage
dtb=os/kbd/arch/arm/boot/dts/c500_cbd_ak3761d_dsi_SRA_704MAHT_v1.0.0.dtb
uboot_bin=os/ubd/u-boot.bin

root_sqsh4=rootfs/root.sqsh4
usr_sqsh4=rootfs/usr.sqsh4
usr_jffs2=rootfs/usr.jffs2	


copy()
{
	cd $SDK

	# echo "cp uImage -> tools/burntool"
	cp -v $uImage $BURNTOOL
	cp -v $dtb $BURNTOOL
	cp -v $uboot_bin $BURNTOOL
	cp -v $root_sqsh4 $BURNTOOL
	cp -v $usr_sqsh4 $BURNTOOL
	cp -v $usr_jffs2  $BURNTOOL

	echo "cp -r tools/burntool-704MAHT -> /mnt/hgfs/share/"
	cp -r tools/burntool-704MAHT /mnt/hgfs/share/
}

backup()
{
	project_path=$(cd $(dirname $0);pwd -P)
	project_name="${project_path##*/}"
	time=$(date +%y%m%d)
	tarName="${project_name}_${time}"

	cd ..
	tar -zcvf $tarName.tar.gz $project_name
    echo "================ <BACKUP DONE> ================"
	pwd
	ls -lh | grep $tarName.tar.gz 
	cp $tarName.tar.gz ~/windows

}



build_kernel()
{
    # 创建编译的目标目录
	if test ! -d $kernel_build_dir ; then
    	mkdir $kernel_build_dir
	fi
   
    
    # 切换到源码目录，准备编译
    cd $KERNEL
	echo "==================================="
	pwd
	echo $kernel_build_dir
	echo $KERNEL_CHIP_TYPE
	echo $CROSS_COMPILE
	echo "==================================="
    
	make O=$kernel_build_dir $KERNEL_CHIP_TYPE CROSS_COMPILE=$CROSS_COMPILE
	make O=$kernel_build_dir dtbs modules uImage -j4 CROSS_COMPILE=$CROSS_COMPILE

    cd $SDK
}


build_uboot()
{
	# 创建编译的目标目录
	if test ! -d $uboot_build_dir ; then
    	mkdir $uboot_build_dir
	fi


	cd $UBOOT

    pwd
	echo $uboot_build_dir
	echo $UBOOT_CHIP_TYPE
	echo $CROSS_COMPILE

	make O=$uboot_build_dir $UBOOT_CHIP_TYPE CROSS_COMPILE=$CROSS_COMPILE
	make O=$uboot_build_dir all -j4 CROSS_COMPILE=$CROSS_COMPILE

	cd $SDK
}

clean_kernel()
{
    rm -rf $kernel_build_dir
}

clean_uboot()
{
	rm -rf $uboot_build_dir
}

make_image()
{
	$SDK/make_image.sh
}

make_env()
{
	# $SDK/tools/envtool/env_build.sh
	cd tools/envtool
	./env_build.sh
}


usage()
{
	echo "========================================================"
	echo "Usage : "
	echo "    build kernel              : ./build.sh -k"
	echo "    build uboot               : ./build.sh -u"
	echo "    make image                : ./build.sh -i"
    echo "    cp files to burntool      : ./build.sh -cp"
	echo "    backup file               : ./build.sh -bk"
	echo ""
	echo "    clean kernel              : ./build.sh -CK"
	echo "    clean uboot               : ./build.sh -CU"
	echo "========================================================"
}




option=$1
if test ! -z $1 ; then
	if test $option = "-k" ; then
		echo "build kernel "
        build_kernel
		exit 0
	fi

	if test $option = "-CK" ; then
		echo "clean kernel "
        clean_kernel
		exit 0
	fi
	
	if test $option = "-u" ; then
		echo "build uboot "
        build_uboot
		exit 0
	fi
    
	if test $option = "-CU" ; then
		echo "clean uboot "
        clean_uboot
		exit 0
	fi

	if test $option = "-i" ; then
		echo "make image "
        make_image
		exit 0
	fi
	
	if test $option = "-cp" ; then
		echo "cp files to burntools "
		copy
		exit 0
	fi
	
	if test $option = "-bk" ; then
		echo "backup file"
		backup
		exit 0
	fi

	if test $option = "-e" ; then
		echo "env_cfg file"
		make_env
		exit 0
	fi
fi

usage


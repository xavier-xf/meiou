#!/bin/sh

# mount_sdcard()
# {   
#     if [ -d /mnt/.upgrade ]; then
#         echo "/mnt/.upgrade exit"
#         umount /mnt/.upgrade
#     else
#         mkdir /mnt/.upgrade
#     fi
#     if [ -b /dev/mmcblk0p1 ]; then
#     mount /dev/mmcblk0p1 /mnt/.upgrade
#     else
#     mount /dev/mmcblk0 /mnt/.upgrade
#     fi
# }









# decompress_firmware()
# {
#     echo "decopress CDV1004QTOS"
#     rm -rf /mnt/.upgrade/tmp
#     mkdir /mnt/.upgrade/tmp

#     cd /mnt/.upgrade
#     tar -xvf CDV1004QTOS -C /mnt/.upgrade/tmp
#     cd /
# }

# run_sd_bin()
# {
#     /mnt/.upgrade/tmp/CDV1004QT.BIN &
# }





# run_flash_bin()
# {
#     /app/app/CDV1004QT.BIN &
# }

# upgrade_firmware()
# {

#     if [ -f /mnt/.upgrade/tmp/CDV1004QT.BIN ];then
#         echo "find CDV1004QT.BIN"
#         run_sd_bin;
#     else
#         run_flash_bin;
#     fi
# }

# detection_firmware()
# {
#     if [ -f /mnt/.upgrade/CDV1004QTOS ]; then
#         decompress_firmware
#         upgrade_firmware
#     else
#         run_flash_bin
#     fi
# }

# echo "Find upgrade firmware......."
# if [ -b /dev/mmcblk0 ]; then
# mount_sdcard
# detection_firmware
# else
# run_flash_bin;
# fi
# echo "finish"







#!/bin/sh

# 运行flash里的app
run_flash_app()
{
    echo "run flash app"
    /app/app/CDV1004QT.BIN &
    echo 0 > /app/data/update_flag
    echo "finish"
    exit
}

# 挂载sd卡到/mnt/tf/目录下
mount_sd_card()
{
    if [ -e /dev/mmcblk0 ]; then
        
        echo "SD card inserted, starting to mount"
        mkdir /mnt/tf
        if [ -b /dev/mmcblk0p1 ]; then
        # umount /dev/mmcblk0p1 /mnt/tf
        mount /dev/mmcblk0p1 /mnt/tf
        echo "SD card inserted,mount p1"
        else
        # umount /dev/mmcblk0 /mnt/tf
        mount /dev/mmcblk0 /mnt/tf
        fi
    else    
        echo "SD card not inserted"
        run_flash_app
    fi
}

# 检测升级包是否存在，存在则启动升级脚本
upgrade()
{
    # if [ -e /mnt/tf/CDV1004QTOS ]; then
    #     echo "Upgrade package exists, starting upgrade"
    #     cp -f /mnt/tf/CDV1004QTOS /tmp
    #     tar -zxvf /tmp/CDV1004QTOS -C /tmp
    #     cd /tmp
    #     echo 1 > /app/data/update_flag
    #     ./update.sh
    # else
    #     echo "Upgrade package does not exist"
        run_flash_app
    # fi
}

  

# /app/data/update_flag 文件是升级的标志，文件不存在 或 内容为1 表示已升级
if [ -e /app/data/update_flag ]; then

    update_flag=$(cat /app/data/update_flag)
    if [ $update_flag -eq 1 ]; then
        echo "Upgraded"
        run_flash_app
    else
        echo "Not upgraded"
        usleep 200000 #延时200ms等待sd卡设备节点产生
        mount_sd_card
        upgrade
    fi
else
    # touch /app/data/update_flag
    run_flash_app
fi


# # /app/data/update_flag 文件是升级的标志，文件不存在 或 内容为1 表示已升级
# usleep 200000 #延时200ms等待sd卡设备节点产生

# mount_sd_card
# if [ -e /app/data/update_flag ]; then

#         echo "Upgraded"
#         rm -rf /app/data/update_flag
#         run_flash_app
        

# elif [ -e /mnt/tf/CDV1004QTOS ]; then
#         echo "Not upgraded"
#         usleep 200000 #延时200ms等待sd卡设备节点产生
#         # mount_sd_card
#         touch /app/data/update_flag
#         upgrade
# else
#     # touch /app/data/update_flag
#     run_flash_app
# fi
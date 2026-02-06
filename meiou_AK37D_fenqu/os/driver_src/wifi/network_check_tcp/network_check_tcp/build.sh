# gcc network_check.c -o network_check_x86
# arm-anykav500-linux-uclibcgnueabi-gcc network_check.c -o network_check_arm -std=c99

# gcc server.c -o network_check_x86
# arm-anykav500-linux-uclibcgnueabi-gcc client.c -o network_check_arm -std=c99

gcc network_check_tcp.c -o network_check_x86
arm-anykav500-linux-uclibcgnueabi-gcc network_check_tcp.c -o network_check_arm -std=c99 -w
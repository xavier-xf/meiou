/*
 * Copyright (c) @CompanyNameMagicTag. 2023. All rights reserved.
 * Description: Main function of sle server sample.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include "sle_common.h"
#include <sle_package.h>

extern sle_addr_t g_Addr;
uint8_t g_server_running = 1;

void signal_handler(int signum)
{
    printf("接收到信号 %d\n", signum);
    g_server_running = 0;
}

int main(int argc, char *argv[])
{
    int ret;
    printf("main start.\n\n");
    signal(SIGINT, signal_handler);   // 注册 SIGINT (Ctrl+C) 信号的处理函数
    signal(SIGTERM, signal_handler);  // 注册 SIGTERM 信号的处理函数
    signal(SIGSEGV, signal_handler);  // 注册 SIGTERM 信号的处理函数
    signal(SIGKILL, signal_handler);  // 注册 SIGTERM 信号的处理函数
    signal(SIGILL, signal_handler);   // 注册 SIGTERM 信号的处理函数
    ret = enable_sle();
    if (ret != 0) {
        printf("enable fail.\n\n");
        goto mainend;
    }
    sle_uuid_server_init();
    while (g_server_running) {
        sleep(5);
    }
    disable_sle();
mainend:
    printf("main end.\n\n");
    return ret;
}
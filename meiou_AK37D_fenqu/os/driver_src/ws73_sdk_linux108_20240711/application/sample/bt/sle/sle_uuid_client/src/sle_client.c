/*
 * Copyright (c) @CompanyNameMagicTag. 2023. All rights reserved.
 * Description: Main function of sle client sample.
 */
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include "dlist.h"
#include "stdint.h"
#include "stdlib.h"
#include <signal.h>
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_uuid_client.h"
#include "sle_package.h"

uint8_t g_running = 1;

void signal_handler(int signum)
{
    printf("接收到信号 %d\n", signum);
    g_running = 0;
}

int main(int argc, char *argv[])
{
    sle_scan_result_t *scan_result = NULL;
    sle_peri_t *peri = NULL;
    size_t index = 0;
    int ret;
    sle_package_t *package = NULL;
    printf("main start.\n\n");
    signal(SIGINT, signal_handler);   // 注册 SIGINT (Ctrl+C) 信号的处理函数
    signal(SIGTERM, signal_handler);  // 注册 SIGTERM 信号的处理函数
    signal(SIGSEGV, signal_handler);  // 注册 SIGTERM 信号的处理函数
    signal(SIGKILL, signal_handler);  // 注册 SIGTERM 信号的处理函数
    signal(SIGILL, signal_handler);   // 注册 SIGTERM 信号的处理函数
    sle_client_init();
#ifdef COMM_MSGQ
    sle_comm_init();
#endif
    sle_start_scan();
    sleep(5);
    sle_stop_scan();
    DLIST_FOREACH(&g_sle_central.sle_scan_result, scan_result, sle_scan_result_t, list)
    {
        printf("Try to connect: [%x]***[%x][%x]\n",
            scan_result->addr.addr[0],
            scan_result->addr.addr[4],
            scan_result->addr.addr[5]);
        peri = sle_connect(&scan_result->addr);
        break;
    }
    if (!peri) {
        printf("Err: peri is NUll\n");
        return -1;
    }
    sle_connection_param_update_t param = {
        .conn_id = peri->conn_id,
        .interval_min = 0x20,
        .interval_max = 0x20,
        .max_latency = 0,
        .supervision_timeout = 500,
    };
    sle_update_connect_param(&param);
    package = calloc(1, PACKAGE_LENGTH);
    for (int i = 0; i < PACKAGE_DATA_LENGTH; i++) {
        package->data[i] = i;
    }
    printf("SLE start transfer:\n  pkg  size: %d\n  data size: %d\n", PACKAGE_LENGTH, PACKAGE_DATA_LENGTH);
    while (g_running) {
        package->peri = peri;
        package->index++;
        package->data_len = PACKAGE_DATA_LENGTH;
#ifdef COMM_MSGQ
        sle_write_msgq((uint8_t *)package, PACKAGE_LENGTH);
#else
        ret = sle_write_sync(peri, (uint8_t *)package, PACKAGE_LENGTH, param.interval_min);
        if (ret) {
            printf("sle_write_sync fail. exit.\n");
            break;
        }
#endif
        sched_yield();
    }
    disable_sle();
mainend:
    printf("main end.\n\n");
    return ret;
}
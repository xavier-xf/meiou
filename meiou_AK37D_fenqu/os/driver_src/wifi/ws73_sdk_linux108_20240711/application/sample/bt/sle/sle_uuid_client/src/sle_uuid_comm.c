/**
 * Copyright (c) @CompanyNameMagicTag 2022. All rights reserved.
 * Description: SLE private service register sample of common.
 */
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "dlist.h"
#include <sle_package.h>
#include "sle_ssap_client.h"
#include <sle_uuid_client.h>

#define MIN(a, b) ((a > b) ? (b) : (a))

extern uint8_t gle_tx_acb_data_num_get(void);
int sle_write_sync(sle_peri_t *peri, uint8_t *data, uint8_t len, uint16_t conn_intval);

typedef struct {
    pthread_mutex_t writeMutex;
    pthread_cond_t writeCond;
    int writeMsgQ;
    pthread_t writeThread;
    int writeThreadStop;
} sle_comm_t;

typedef struct {
    long mtype;                 /* message type, must be > 0 */
    char mtext[PACKAGE_LENGTH]; /* message data */
} msgbuf_t;

static sle_comm_t g_sle_comm = {0};

static void *sle_writeThread(void *arg)
{
    struct timespec time_to_wait;
    struct msqid_ds msqQState;
    msgbuf_t msgBuf;
    ssize_t recv_size;
    sle_peri_t *peri = NULL;
    int buf_cnt;
    int tx_cnt;
    int ret;
    printf("sle_writeThread start...\n");
    while (!g_sle_comm.writeThreadStop) {
        // 获取互斥锁
        pthread_mutex_lock(&g_sle_comm.writeMutex);

        // 等待条件变量的信号或者指定时间到达
        while (!g_sle_comm.writeThreadStop) {
            // 获取当前时间
            clock_gettime(CLOCK_MONOTONIC, &time_to_wait);
            // time_to_wait.tv_sec += 1;//延时1s
            time_to_wait.tv_nsec += 4 * 1000 * 1000;  // 休眠10ms
            (void)pthread_cond_timedwait(&g_sle_comm.writeCond, &g_sle_comm.writeMutex, &time_to_wait);
            /* 如果发送buf没有空闲位置，则继续休眠 */
            buf_cnt = gle_tx_acb_data_num_get();
            if (!buf_cnt) {
                continue;
            }
            /* 如果消息队列中有数据，则跳出，执行发送程序 */
            msgctl(g_sle_comm.writeMsgQ, IPC_STAT, &msqQState);
            if (msqQState.msg_qnum) {
                break;
            }
            tx_cnt = MIN(buf_cnt, msqQState.msg_qnum);
        }
        while (tx_cnt--) {
            /* 尝试从消息队列中取出数据 */
            recv_size = msgrcv(g_sle_comm.writeMsgQ, &msgBuf, sizeof(msgBuf.mtext), 0, IPC_NOWAIT | MSG_NOERROR);
            if (recv_size <= 0) {
                printf("Err: msgrcv retrun size is 0.\n");
                pthread_mutex_unlock(&g_sle_comm.writeMutex);
                continue;
            }
            /* 发送数据 */
            peri = (sle_peri_t *)((sle_package_t *)&msgBuf)->peri;
            ret = sle_write_req(peri, (uint8_t *)&msgBuf, recv_size + sizeof(long));
            if (ret < 0) {
                printf("Err: sle_write_req fail.\n");
            }
        }
        // 释放互斥锁
        pthread_mutex_unlock(&g_sle_comm.writeMutex);
    }
    return NULL;
}

int sle_comm_init()
{
    int ret;
    pthread_mutex_init(&g_sle_comm.writeMutex, NULL);
    pthread_cond_init(&g_sle_comm.writeCond, NULL);
    g_sle_comm.writeMsgQ = msgget(IPC_PRIVATE, IPC_CREAT);
    if (g_sle_comm.writeMsgQ < 0) {
        printf("Err: fail to create msgQ for write thread.\n");
        goto free_mux_cond;
    }
    g_sle_comm.writeThreadStop = false;
    ret = pthread_create(&g_sle_comm.writeThread, NULL, sle_writeThread, NULL);
    if (ret < 0) {
        printf("Err: fail to create write thread.\n");
        goto free_msgq;
    }
    printf("sle_comm_init.\n");
    return 0;
free_msgq:
    msgctl(g_sle_comm.writeMsgQ, IPC_RMID, NULL);
free_mux_cond:
    pthread_mutex_destroy(&g_sle_comm.writeMutex);
    pthread_cond_destroy(&g_sle_comm.writeCond);
    return -1;
}

void sle_comm_deinit()
{
    g_sle_comm.writeThreadStop = true;
    pthread_join(g_sle_comm.writeThread, NULL);
    pthread_mutex_destroy(&g_sle_comm.writeMutex);
    pthread_cond_destroy(&g_sle_comm.writeCond);
    msgctl(g_sle_comm.writeMsgQ, IPC_RMID, NULL);
    return;
}

int sle_write_msgq(uint8_t *data, uint8_t len)
{
    int ret;
    /* 将数据发送到消息队列 */
    ((sle_package_t *)data)->mtype = 1;
    ret = msgsnd(g_sle_comm.writeMsgQ, data, len - sizeof(long), 0);
    if (ret < 0) {
        perror("sle_write_msgq faild.");
    }
    /* 启动发送线程 */
    pthread_cond_signal(&g_sle_comm.writeCond);
    return ret;
}

int sle_write_sync(sle_peri_t *peri, uint8_t *data, uint8_t len, uint16_t conn_intval)
{
    int retry = 20;
    struct timespec time;
    while (!gle_tx_acb_data_num_get() && retry--) {
        if (!conn_intval)
            usleep(1000 * 10);  // sleep 10ms default
        else
            usleep(125 * conn_intval);
        printf("\n\nEric:num=%d, try=%d\n\n", gle_tx_acb_data_num_get(), retry);
    }
    printf("\n\nEric: sle_write_sync s: num=%d\n\n", gle_tx_acb_data_num_get());
    if (retry < 0) {
        printf("Err: sle_write_sync fail.\n");
        return -1;
    }
    return sle_write_req(peri, (uint8_t *)data, len);
}

int sle_change_phy_2M(uint16_t handle)
{
    sle_set_phy_t phy_param = {0};
    /* 0:1M, 1:2M, 2:4M */
    phy_param.tx_phy = 1;
    phy_param.rx_phy = 1;
    sle_set_phy_param(handle, &phy_param);
    return 0;
}
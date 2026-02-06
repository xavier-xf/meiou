/*
 * SLE main entry
 */
#include <stdint.h>
#include <pthread.h>
#include "soc_osal.h"
#include "bsle_dft_at_server.h"

void sle_hci_recv_data_task(void)
{
    uint8_t *data_buf = NULL;
    uint32_t data_len = 0;
    int ret;

    for (;;) {
        if (data_buf != NULL && data_len > 0) {
            free(data_buf);
            data_buf = NULL;
            data_len = 0;
        }
        ret = sle_hci_chan_recv_data(&data_buf, &data_len);
        if (ret != 0) {
            printf("sle_hci_recv_data_task failed:0x%x\n", ret);
            continue;
        }
        printf("CMD Send OK\n");
        for (int i = 0; i < data_len; i++) {
            printf("%02X ", data_buf[i]);
        }
        printf("\n");
    }
}

void ble_hci_recv_data_task(void)
{
    uint8_t *data_buf = NULL;
    uint32_t data_len = 0;
    int ret;

    for (;;) {
        if (data_buf != NULL && data_len > 0) {
            free(data_buf);
            data_buf = NULL;
            data_len = 0;
        }
        ret = ble_hci_chan_recv_data(&data_buf, &data_len);
        if (ret != 0) {
            printf("ble_hci_recv_data_task failed:0x%x\n", ret);
            continue;
        }
        printf("CMD Send OK\n");
        for (int i = 0; i < data_len; i++) {
            printf("%02X ", data_buf[i]);
        }
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    printf("main start.\n\n");
    sle_chan_init();
    pthread_t thread_id = (pthread_t)(-1);
    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
    if (pthread_create(&thread_id, &thread_attr, sle_hci_recv_data_task, NULL) != 0) {
        printf("create sle_hci_recv_data_task fail.\n");
        return (pthread_t)(-1);
    }
    if (pthread_create(&thread_id, &thread_attr, ble_hci_recv_data_task, NULL) != 0) {
        printf("create ble_hci_recv_data_task fail.\n");
        return (pthread_t)(-1);
    }
    cmd_main_add_functions();
    printf("main end.\n\n");
    return 0;
}

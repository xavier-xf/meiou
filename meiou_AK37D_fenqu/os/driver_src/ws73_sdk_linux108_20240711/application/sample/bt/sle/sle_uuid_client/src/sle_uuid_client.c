/**
 * Copyright (c) @CompanyNameMagicTag 2022. All rights reserved.
 *
 * Description: SLE private service register sample of client.
 */
#include <stddef.h>
#include <stdbool.h>
#include "dlist.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sched.h>
#include "securec.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_uuid_client.h"

#undef THIS_FILE_ID
#define THIS_FILE_ID BTH_GLE_SAMPLE_UUID_CLIENT

#define SLE_MTU_SIZE_DEFAULT        251
#define SLE_SEEK_INTERVAL_DEFAULT   160
#define SLE_SEEK_WINDOW_DEFAULT     40
#define UUID_16BIT_LEN              2
#define UUID_128BIT_LEN             16

sle_announce_seek_callbacks_t g_seek_cbk = {0};
sle_connection_callbacks_t g_connect_cbk = {0};
ssapc_callbacks_t g_ssapc_cbk = {0};

sle_central_t g_sle_central = {0};
void state_machine_event(sle_event_t event);

void sle_sample_sle_enable_cbk(errcode_t status)
{
    if (status == 0) {
        printf("sle sample sle enable success.\r\n");
    }
}

void sle_sample_seek_enable_cbk(errcode_t status)
{
    if (status == 0) {
        printf("sle sample seek enable success.\r\n");
    }
}

void sle_sample_seek_disable_cbk(errcode_t status)
{
    if (status == 0) {
        printf("sle sample seek disable success.\r\n");
    }
}

void sle_sample_seek_result_info_cbk(sle_seek_result_info_t *seek_result_data)
{
    sle_scan_result_t *result = NULL;
    sle_scan_result_t *tmp_result = NULL;
    printf("sle_sample_seek_result_info_cbk enter.\r\n");
    if (seek_result_data != NULL) {
        /* 如果设备已经存在，则跳过 */
        DLIST_FOREACH(&g_sle_central.sle_scan_result, tmp_result, sle_scan_result_t, list)
        {
            if (!memcmp(tmp_result->addr.addr, seek_result_data->addr.addr, 6)) {
                printf("sle_sample_seek_result_info_cbk device exist.\n");
                return;
            }
        }
        result = calloc(1, sizeof(sle_scan_result_t));
        if (!result) {
            printf("Err: calloc fail for seek result.\n");
            state_machine_event(EVENT_SCAN_FAIL);
            return;
        }
        printf("add new seek result in list.\n");
        (void)memcpy_s(&result->addr, sizeof(sle_addr_t), &seek_result_data->addr, sizeof(sle_addr_t));
        dlist_node_append(&g_sle_central.sle_scan_result, &result->list);
    }
    return;
}

void sle_sample_seek_cbk_register(void)
{
    g_seek_cbk.sle_enable_cb = sle_sample_sle_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_sample_seek_enable_cbk;
    g_seek_cbk.seek_disable_cb = sle_sample_seek_disable_cbk;
    g_seek_cbk.seek_result_cb = sle_sample_seek_result_info_cbk;
}

void sle_sample_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr, sle_acb_state_t conn_state,
    sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        if (!g_sle_central.sle_target_peri) {
            printf("error: g_sle_central->sle_target_peri is NULL.\n");
            state_machine_event(EVENT_CONNECT_FAIL);
            return;
        }
        g_sle_central.sle_target_peri->conn_id = conn_id;
        state_machine_event(EVENT_CONNECT_CMPL);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_sle_central.sle_target_peri->conn_id = -1;
        printf("SLE disconnect. reason=0x%x\n", disc_reason);
        state_machine_event(EVENT_DISCONNECT);
    }
}

void sle_sample_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    if (status == 0) {
        state_machine_event(EVENT_PAIR_CMPL);
    }
}

void sle_sample_connect_cbk_register(void)
{
    g_connect_cbk.connect_state_changed_cb = sle_sample_connect_state_changed_cbk;
    g_connect_cbk.pair_complete_cb = sle_sample_pair_complete_cbk;
}

void sle_sample_exchange_info_cbk(uint8_t client_id, uint16_t conn_id, ssap_exchange_info_t *param, errcode_t status)
{
    state_machine_event(EVENT_EXCHANGE_CMPL);
}

void sle_sample_find_structure_cbk(
    uint8_t client_id, uint16_t conn_id, ssapc_find_service_result_t *service, errcode_t status)
{
    printf("[ssap client] find structure cbk client: %d conn_id:%d status: %d \n", client_id, conn_id, status);
    printf("[ssap client] find structure start_hdl:[0x%02x], end_hdl:[0x%02x], uuid len:%d\r\n",
        service->start_hdl,
        service->end_hdl,
        service->uuid.len);
    if (service->uuid.len == UUID_16BIT_LEN) {
        printf("[ssap client] structure uuid:[0x%02x][0x%02x]\r\n",
            service->uuid.uuid[14],
            service->uuid.uuid[15]); /* 14 15: uuid index */
    } else {
        for (uint8_t idx = 0; idx < UUID_128BIT_LEN; idx++) {
            printf("[ssap client] structure uuid[%d]:[0x%02x]\r\n", idx, service->uuid.uuid[idx]);
        }
    }
    g_sle_central.sle_target_peri->service.start_hdl = service->start_hdl;
    g_sle_central.sle_target_peri->service.end_hdl = service->end_hdl;
    memcpy_s(&g_sle_central.sle_target_peri->service.uuid, sizeof(sle_uuid_t), &service->uuid, sizeof(sle_uuid_t));
}

void sle_sample_find_structure_cmp_cbk(
    uint8_t client_id, uint16_t conn_id, ssapc_find_structure_result_t *structure_result, errcode_t status)
{
    printf("[ssap client] find structure cmp cbk client id:%d status:%d type:%d uuid len:%d \r\n",
        client_id,
        status,
        structure_result->type,
        structure_result->uuid.len);
    if (structure_result->uuid.len == UUID_16BIT_LEN) {
        printf("[ssap client] find structure cmp cbk structure uuid:[0x%02x][0x%02x]\r\n",
            structure_result->uuid.uuid[14],
            structure_result->uuid.uuid[15]); /* 14 15: uuid index */
    } else {
        for (uint8_t idx = 0; idx < UUID_128BIT_LEN; idx++) {
            printf("[ssap client] find structure cmp cbk structure uuid[%d]:[0x%02x]\r\n",
                idx,
                structure_result->uuid.uuid[idx]);
        }
    }
    state_machine_event(EVENT_DISCOVER_CMPL);
}

void sle_sample_find_property_cbk(
    uint8_t client_id, uint16_t conn_id, ssapc_find_property_result_t *property, errcode_t status)
{
    printf("[ssap client] find property cbk, client id: %d, conn id: %d, operate ind: %d, "
           "descriptors count: %d status:%d.\n",
        client_id,
        conn_id,
        property->operate_indication,
        property->descriptors_count,
        status);
    for (uint16_t idx = 0; idx < property->descriptors_count; idx++) {
        printf(
            "[ssap client] find property cbk, descriptors type [%d]: 0x%02x.\n", idx, property->descriptors_type[idx]);
    }
    if (property->uuid.len == UUID_16BIT_LEN) {
        printf("[ssap client] find property cbk, uuid: %02x %02x.\n",
            property->uuid.uuid[14],
            property->uuid.uuid[15]); /* 14 15: uuid index */
    } else if (property->uuid.len == UUID_128BIT_LEN) {
        for (uint16_t idx = 0; idx < UUID_128BIT_LEN; idx++) {
            printf("[ssap client] find property cbk, uuid [%d]: %02x.\n", idx, property->uuid.uuid[idx]);
        }
    }
}

void sle_sample_write_cfm_cbk(uint8_t client_id, uint16_t conn_id, ssapc_write_result_t *write_result, errcode_t status)
{
    printf("[ssap client] write cfm cbk, client id: %d status:%d.\n", client_id, status);
    ssapc_read_req(0, conn_id, write_result->handle, write_result->type);
}

void sle_sample_read_cfm_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *read_data, errcode_t status)
{
    printf("[ssap client] read cfm cbk client id: %d conn id: %d status: %d\n", client_id, conn_id, status);
    printf("[ssap client] read cfm cbk handle: %d, type: %d , len: %d\n",
        read_data->handle,
        read_data->type,
        read_data->data_len);
    for (uint16_t idx = 0; idx < read_data->data_len; idx++) {
        printf("[ssap client] read cfm cbk[%d] 0x%02x\r\n", idx, read_data->data[idx]);
    }
}

void sle_notification_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status)
{
    printf("[ssap client] sle_notification_cbk, client id: %d conn id: %d status: %d\n", client_id, conn_id, status);
}

void sle_sample_ssapc_cbk_register(void)
{
    g_ssapc_cbk.exchange_info_cb = sle_sample_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_sample_find_structure_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = sle_sample_find_structure_cmp_cbk;
    g_ssapc_cbk.ssapc_find_property_cbk = sle_sample_find_property_cbk;
    g_ssapc_cbk.write_cfm_cb = sle_sample_write_cfm_cbk;
    g_ssapc_cbk.read_cfm_cb = sle_sample_read_cfm_cbk;
    g_ssapc_cbk.notification_cb = sle_notification_cbk;
}

void state_machine_func(sle_event_t event)
{
    sle_state_t old_state;
    static int try_cnt = 0;
    old_state = g_sle_central.state;
    printf("state_machine: ev=0x%x\n", event);

    switch (g_sle_central.state) {
        case STATE_IDLE:
            if (event == EVENT_SCAN_TRIG) {
                g_sle_central.state = STATE_SEEKING;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            } else if (event == EVENT_CONNECT_TRIG) {
                if (!g_sle_central.sle_target_peri) {
                    printf("Err: No target peri specified.\n");
                    break;
                }
                g_sle_central.state = STATE_CONNECTING;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            }
            break;
        case STATE_SEEKING:
            if ((event == EVENT_STATE_CHANGE) || ((event == EVENT_SCAN_FAIL) && (try_cnt++ < 3))) {
                sle_seek_param_t param = {0};
                param.own_addr_type = 0;
                param.filter_duplicates = 0;
                param.seek_filter_policy = 0;
                param.seek_phys = 1;
                param.seek_type[0] = 0;
                param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
                param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
                sle_set_seek_param(&param);
                sle_start_seek();
            } else if (event == EVENT_SCAN_CMPL) {
                g_sle_central.state = STATE_IDLE;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            } else {
                g_sle_central.state = STATE_IDLE;
                printf("Err: sle scan fail.\n");
            }
            break;
        case STATE_CONNECTING:
            if ((event == EVENT_STATE_CHANGE) ||
                (((event == EVENT_CONNECT_FAIL) || (event == EVENT_DISCONNECT)) && (try_cnt++ < 3))) {
                sle_connect_remote_device(&g_sle_central.sle_target_peri->addr);
            } else if (event == EVENT_CONNECT_CMPL) {
                g_sle_central.state = STATE_PAIRING;
                try_cnt = 0;
                sle_change_phy_2M(g_sle_central.sle_target_peri->conn_id);
                state_machine_event(EVENT_STATE_CHANGE);
            } else {
                g_sle_central.state = STATE_IDLE;
                printf("Err: connect fail, event=0x%x\n", event);
            }
            break;
        case STATE_PAIRING:
            if ((event == EVENT_STATE_CHANGE) || ((event == EVENT_PAIR_FAIL) && (try_cnt++ < 3))) {
                sle_pair_remote_device(&g_sle_central.sle_target_peri->addr);
            } else if (event == EVENT_PAIR_CMPL) {
                g_sle_central.state = STATE_EXCHANING;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            } else {
                g_sle_central.state = STATE_IDLE;
                printf("Err: pair fail, event=0x%x\n", event);
            }
            break;
        case STATE_EXCHANING:
            if ((event == EVENT_STATE_CHANGE) || ((event == EVENT_EXCHANGE_FAIL) && (try_cnt++ < 3))) {
                ssap_exchange_info_t info = {0};
                info.mtu_size = SLE_MTU_SIZE_DEFAULT;
                info.version = 1;
                ssapc_exchange_info_req(1, g_sle_central.sle_target_peri->conn_id, &info);
            } else if (event == EVENT_EXCHANGE_CMPL) {
                g_sle_central.state = STATE_DISCOVERING;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            } else {
                g_sle_central.state = STATE_IDLE;
                printf("Err: exchange fail, event=0x%x\n", event);
            }
            break;
        case STATE_DISCOVERING:
            if ((event == EVENT_STATE_CHANGE) || ((event == EVENT_DISCOVER_FAIL) && (try_cnt++ < 3))) {
                ssapc_find_structure_param_t find_param = {0};
                find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
                find_param.start_hdl = 1;
                find_param.end_hdl = 0xFFFF;
                ssapc_find_structure(0, g_sle_central.sle_target_peri->conn_id, &find_param);
            } else if (event == EVENT_DISCOVER_CMPL) {
                g_sle_central.state = STATE_TRANSFER;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            } else {
                g_sle_central.state = STATE_IDLE;
                printf("Err: discover service fail, event=0x%x\n", event);
            }
            break;
        case STATE_TRANSFER:
            if (event == EVENT_STATE_CHANGE) {
                dlist_node_append(&g_sle_central.sle_peri, &g_sle_central.sle_target_peri->list);
            } else if (event == EVENT_DISCONNECT) {
                g_sle_central.state = STATE_IDLE;
                try_cnt = 0;
                state_machine_event(EVENT_STATE_CHANGE);
            }
        default:
            break;
    }
    printf("state_machine: state change [0x%x -> 0x%x]\n", old_state, g_sle_central.state);
    return;
}

void *state_machine_runner(void *data)
{
    struct st_event buf = {0};
    int ret;
    while (!g_sle_central.state_machine_quit) {
        memset(&buf, 0, sizeof(buf));
        ret = msgrcv(g_sle_central.mq, &buf, sizeof(buf.event), 0, 0);
        if ((ret < 0) && (errno == EINTR)) {
            continue;
        } else if (ret < 0) {
            break;
        }
        state_machine_func(buf.event);
    }
    return NULL;
}

void state_machine_event(sle_event_t event)
{
    struct st_event buf = {0};
    buf.mtype = 1;
    buf.event = event;
    while (msgsnd(g_sle_central.mq, &buf, sizeof(buf.event), 0) < 0) {
        if (errno == EINTR)
            continue;
        return;
    }
    sched_yield();
    return;
}

void sle_client_init()
{
    int ret;
    memset(&g_sle_central, 0, sizeof(g_sle_central));
    INIT_LIST_HEAD(&g_sle_central.sle_scan_result);
    INIT_LIST_HEAD(&g_sle_central.sle_peri);
    g_sle_central.state = STATE_IDLE;
    g_sle_central.state_machine_quit = false;
    g_sle_central.mq = msgget(IPC_PRIVATE, IPC_CREAT | S_IRUSR | S_IWUSR);
    if (g_sle_central.mq < 0) {
        printf("Err: systemV mq request fail.\n");
        return;
    }
    ret = pthread_create(&g_sle_central.state_machine_thread, NULL, state_machine_runner, NULL);
    if (ret < 0) {
        printf("Err: fail to create thread for state machine.\n");
        return;
    }
    sle_sample_seek_cbk_register();
    sle_sample_connect_cbk_register();
    sle_sample_ssapc_cbk_register();
    sle_announce_seek_register_callbacks(&g_seek_cbk);
    sle_connection_register_callbacks(&g_connect_cbk);
    ssapc_register_callbacks(&g_ssapc_cbk);
    enable_sle();
    return;
}

void sle_start_scan(void)
{
    state_machine_event(EVENT_SCAN_TRIG);
    return;
}

void sle_stop_scan(void)
{
    sle_stop_seek();
    state_machine_event(EVENT_SCAN_CMPL);
    return;
}

sle_peri_t *sle_connect(sle_addr_t *addr)
{
    g_sle_central.sle_target_peri = calloc(1, sizeof(sle_peri_t));
    if (!g_sle_central.sle_target_peri) {
        printf("Err: calloc for g_sle_central.sle_target_peri fail\n");
        return NULL;
    }
    g_sle_central.sle_target_peri->addr = *addr;
    g_sle_central.state = STATE_IDLE;
    state_machine_event(EVENT_CONNECT_TRIG);
    sleep(2);
    printf("sle_connect state: %x\n", g_sle_central.state);
    for (int i = 0; i < 60; i++) {
        printf("sle_connect foreach. i=%d\n", i);
        if (g_sle_central.state == STATE_TRANSFER) {
            return g_sle_central.sle_target_peri;
        } else if (g_sle_central.state == STATE_IDLE) {
            free(g_sle_central.sle_target_peri);
            printf("Err: fail to connect to devcie.\n");
            return NULL;
        } else {
            printf("sle_connect sleep.\n");
            sleep(1);
        }
    }
    free(g_sle_central.sle_target_peri);
    return NULL;
}

int sle_write_req(sle_peri_t *peri, uint8_t *data, uint8_t len)
{
    ssapc_write_param_t param = {0};
    if ((!peri) || (!data)) {
        printf("Err: peri or data NULL\n");
        return -1;
    }
    param.handle = peri->service.start_hdl;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = len;
    param.data = data;
    return ssapc_write_cmd(0, peri->conn_id, &param);
}
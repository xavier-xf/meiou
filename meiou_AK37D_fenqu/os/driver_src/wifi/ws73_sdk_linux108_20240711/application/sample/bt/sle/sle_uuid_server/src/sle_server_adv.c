/*
 * Copyright (c) @CompanyNameMagicTag. 2023. All rights reserved.
 * Description: sle adv config for sle uuid server.
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "securec.h"
#include "errcode.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_package.h"
#include "sle_server_adv.h"

/* sle device name */
#define NAME_MAX_LENGTH 15
/* 连接调度间隔12.5ms，单位125us */
#define SLE_CONN_INTV_MIN_DEFAULT 0x64
/* 连接调度间隔12.5ms，单位125us */
#define SLE_CONN_INTV_MAX_DEFAULT 0x64
/* 连接调度间隔25ms，单位125us */
#define SLE_ADV_INTERVAL_MIN_DEFAULT 0xC8
/* 连接调度间隔25ms，单位125us */
#define SLE_ADV_INTERVAL_MAX_DEFAULT 0x190
/* 超时时间5000ms，单位10ms */
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT 0x1F4
/* 超时时间4990ms，单位10ms */
#define SLE_CONN_MAX_LATENCY 0x1F3
/* 广播发送功率 */
#define SLE_ADV_TX_POWER 10
/* 广播ID */
#define SLE_ADV_HANDLE_DEFAULT 1
/* 最大广播数据长度 */
#define SLE_ADV_DATA_LEN_MAX 251
/* 广播名称 */
uint8_t sle_local_name[NAME_MAX_LENGTH] = {'s', 'l', 'e', '_', 'u', 'u', 'i', 'd', '_', 's', 'e', 'r', 'v', 'e', 'r'};
static sle_addr_t sle_addr = {
    .type = 0x00,
    .addr = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06},
};

static uint16_t sle_set_adv_local_name(uint8_t *adv_data, uint16_t max_len)
{
    errno_t ret;
    uint8_t index = 0;
    uint8_t *local_name = sle_local_name;
    uint8_t local_name_len = (uint8_t)strlen((char *)local_name);
    for (uint8_t i = 0; i < local_name_len; i++) {
        printf("local_name[%d] = 0x%02x\r\n", i, local_name[i]);
    }
    adv_data[index++] = local_name_len + 1;
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    ret = memcpy_s(&adv_data[index], max_len - index, local_name, local_name_len);
    if (ret != EOK) {
        printf("memcpy fail\r\n");
        return 0;
    }
    return (uint16_t)index + local_name_len;
}

static uint16_t sle_set_adv_data(uint8_t *adv_data)
{
    size_t len = 0;
    uint16_t idx = 0;
    errno_t ret = 0;
    len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_disc_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, len);
    if (ret != EOK) {
        printf("adv_disc_level memcpy fail\r\n");
        return 0;
    }
    idx += len;
    len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value adv_access_mode = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0,
    };
    ret = memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_access_mode, len);
    if (ret != EOK) {
        printf("memcpy fail\r\n");
        return 0;
    }
    idx += len;
    return idx;
}

static uint16_t sle_set_scan_response_data(uint8_t *scan_rsp_data)
{
    uint16_t idx = 0;
    errno_t ret;
    size_t scan_rsp_data_len = sizeof(struct sle_adv_common_value);
    struct sle_adv_common_value tx_power_level = {
        .length = scan_rsp_data_len - 1,
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER,
    };
    ret = memcpy_s(scan_rsp_data, SLE_ADV_DATA_LEN_MAX, &tx_power_level, scan_rsp_data_len);
    if (ret != EOK) {
        printf("sle scan response data memcpy fail\r\n");
        return 0;
    }
    idx += scan_rsp_data_len;
    /* set local name */
    idx += sle_set_adv_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
    return idx;
}

static int sle_set_default_announce_param(void)
{
    sle_announce_param_t param = {0};
    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = SLE_ADV_HANDLE_DEFAULT;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_ADV_INTERVAL_MIN_DEFAULT;
    param.announce_interval_max = SLE_ADV_INTERVAL_MAX_DEFAULT;
    param.conn_interval_min = SLE_CONN_INTV_MIN_DEFAULT;
    param.conn_interval_max = SLE_CONN_INTV_MAX_DEFAULT;
    param.conn_max_latency = SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT;
    param.own_addr = sle_addr;
    return sle_set_announce_param(param.announce_handle, &param);
}

static int sle_set_default_announce_data(void)
{
    errcode_t ret;
    uint8_t announce_data_len = 0;
    uint8_t seek_data_len = 0;
    sle_announce_data_t data = {0};
    uint8_t adv_handle = SLE_ADV_HANDLE_DEFAULT;
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = {0};
    printf("set adv data default\r\n");
    announce_data_len = sle_set_adv_data(announce_data);
    data.announce_data = announce_data;
    data.announce_data_len = announce_data_len;
    seek_data_len = sle_set_scan_response_data(seek_rsp_data);
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = seek_data_len;
    ret = sle_set_announce_data(adv_handle, &data);
    if (ret == ERRCODE_SLE_SUCCESS) {
        printf("[SLE DD SDK] set announce data success.");
    } else {
        printf("[SLE DD SDK] set adv param fail.");
    }
    return ERRCODE_SLE_SUCCESS;
}

void sle_announce_enable_cbk(uint32_t announce_id, errcode_t status)
{
    printf("sle announce enable id:%02x, state:%02x\r\n", announce_id, status);
}

void sle_announce_disable_cbk(uint32_t announce_id, errcode_t status)
{
    printf("sle announce disable id:%02x, state:%02x\r\n", announce_id, status);
}

void sle_announce_terminal_cbk(uint32_t announce_id)
{
    printf("sle announce terminal id:%02x\r\n", announce_id);
}

void sle_enable_cbk(errcode_t status)
{
    printf("sle enable status:%02x\r\n", status);
}

void sle_announce_register_cbks(void)
{
    sle_announce_seek_callbacks_t seek_cbks = {0};
    seek_cbks.announce_enable_cb = sle_announce_enable_cbk;
    seek_cbks.announce_disable_cb = sle_announce_disable_cbk;
    seek_cbks.announce_terminal_cb = sle_announce_terminal_cbk;
    seek_cbks.sle_enable_cb = sle_enable_cbk;
    sle_announce_seek_register_callbacks(&seek_cbks);
}

void sle_set_random_mac_addr(void)
{
    int randomData;
    randomData = open("/dev/urandom", O_RDONLY);
    if (randomData < 0) {
        perror("Failed to open /dev/urandom");
        return;
    }
    ssize_t bytesRead = read(randomData, sle_addr.addr, sizeof(sle_addr.addr));
    if (bytesRead != sizeof(sle_addr.addr)) {
        perror("Failed to read from /dev/urandom");
        close(randomData);
        return;
    }
    close(randomData);
    printf("sle set random mac addr: ");
    for (int i = 0; i < sizeof(sle_addr.addr); i++) {
        printf("%02X ", sle_addr.addr[i]);
    }
    printf("\n");
    sle_set_local_addr(&sle_addr);
    return;
}
void sle_set_dev_name(void)
{
    errcode_t ret;
    static uint8_t device_name[] = {"SLE_CAMERA"};
    ret = sle_set_local_name(device_name, sizeof(device_name));
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("[dis server] set local name fail, ret:%x\r\n", ret);
        return;
    }
    return;
}

void sle_uuid_server_adv_enable(void)
{
    sle_set_default_announce_param();
    sle_set_default_announce_data();
    sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    return;
}

errcode_t sle_uuid_server_adv_init(void)
{
    printf("sle_uuid_server_adv_init in\r\n");
    sle_announce_register_cbks();
    sle_set_dev_name();
    sle_uuid_server_adv_enable();
    printf("sle_uuid_server_adv_init out\r\n");
    return ERRCODE_SLE_SUCCESS;
}
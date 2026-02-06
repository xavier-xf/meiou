/*
 * Copyright (c) @CompanyNameMagicTag. 2022. All rights reserved.
 * Description: ble uuid server sample.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "osal_addr.h"
#include "bts_def.h"
#include "securec.h"
#include "errcode.h"
#include "test_suite_uart.h"
#include "bts_def.h"
#include "bts_le_gap.h"
#include "bts_gatt_stru.h"
#include "bts_gatt_server.h"
#include "ble_server_adv.h"
#include "bts_device_manager.h"
#include "ble_uuid_server.h"

/* server app uuid for test */
char g_uuid_app_uuid[] = {0x0, 0x0};

/* ble indication att handle */
uint16_t g_indication_characteristic_att_hdl = 0;

/* ble notification att handle */
uint16_t g_notification_characteristic_att_hdl = 0;

/* ble connect handle */
static uint16_t g_conn_hdl = 0;

/* ble connect count */
static uint16_t g_connect_cnt = 0;

/* ble service count */
static uint8_t g_service_cnt = 0;

#define OCTET_BIT_LEN 8
#define UUID_LEN_2 2
#define BLE_SLEEP_US 10000

#define sample_at_log_print(fmt, args...) test_suite_uart_sendf(fmt, ##args)

/* 将uint16的uuid数字转化为bt_uuid_t */
void stream_data_to_uuid(uint16_t uuid_data, bt_uuid_t *out_uuid)
{
    char uuid[] = {(uint8_t)(uuid_data >> OCTET_BIT_LEN), (uint8_t)uuid_data};
    out_uuid->uuid_len = UUID_LEN_2;
    if (memcpy_s(out_uuid->uuid, out_uuid->uuid_len, uuid, UUID_LEN_2) != EOK) {
        return;
    }
}

/* 添加发送服务的所有特征和描述符 */
static void ble_uuid_server_add_tx_characters_and_descriptors(uint8_t server_id, uint16_t srvc_handle)
{
    sample_at_log_print("[uuid server] TX characters, server_id: %hhu, srv_handle: %hu \n",
        server_id, srvc_handle);
    bt_uuid_t characters_uuid = { 0 };
    uint8_t characters_value[] = { 0x54, 0x58, 0x63};
    stream_data_to_uuid(BLE_UUID_CHARACTERISTIC_UUID_TX, &characters_uuid);
    gatts_add_chara_info_t character;
    gatts_add_character_result_t result = { 0 };
    uint16_t handle = 0;
    character.chara_uuid = characters_uuid;
    character.properties = GATT_CHARACTER_PROPERTY_BIT_NOTIFY | GATT_CHARACTER_PROPERTY_BIT_READ;
    character.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    character.value_len = sizeof(characters_value);
    character.value = characters_value;
    gatts_add_characteristic_sync(server_id, srvc_handle, &character, &result);
    sample_at_log_print("[uuid server] characters uuid: %02x %02x\n",
        characters_uuid.uuid[0], characters_uuid.uuid[1]);
 
    g_indication_characteristic_att_hdl = result.value_handle;

    static uint8_t ccc_val[] = { 0x54, 0x58, 0x64 }; // notify
    bt_uuid_t ccc_uuid = { 0 };
    stream_data_to_uuid(BLE_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION, &ccc_uuid);
    gatts_add_desc_info_t descriptor;
    descriptor.desc_uuid = ccc_uuid;
    descriptor.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    descriptor.value_len = sizeof(ccc_val);
    descriptor.value = ccc_val;
    gatts_add_descriptor_sync(server_id, srvc_handle, &descriptor, &handle);
 
}
 
/* 添加接收服务的所有特征和描述符 */
static void ble_uuid_server_add_rx_characters_and_descriptors(uint8_t server_id, uint16_t srvc_handle)
{
    sample_at_log_print("[uuid server] RX characters, server_id: %hhu, srv_handle: %hu \n",
        server_id, srvc_handle);
    bt_uuid_t characters_uuid = { 0 };
    uint8_t characters_value[] = { 0x52, 0x58, 0x63 };
    stream_data_to_uuid(BLE_UUID_CHARACTERISTIC_UUID_RX, &characters_uuid);
    gatts_add_chara_info_t character;
    gatts_add_character_result_t result = { 0 };
    uint16_t handle = 0;
    character.chara_uuid = characters_uuid;
    character.properties = GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE_NO_RSP;
    character.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    character.value_len = sizeof(characters_value);
    character.value = characters_value;
 
    gatts_add_characteristic_sync(server_id, srvc_handle, &character, &result);
    sample_at_log_print("[uuid server] characters uuid: %02x %02x\n",
        characters_uuid.uuid[0], characters_uuid.uuid[1]);
 
    bt_uuid_t ccc_uuid = { 0 };
    /* client characteristic configuration value for test */
    static uint8_t ccc_val[] = { 0x52, 0x58, 0x64 };
    stream_data_to_uuid(BLE_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION, &ccc_uuid);
    gatts_add_desc_info_t descriptor;
    descriptor.desc_uuid = ccc_uuid;
    descriptor.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
    descriptor.value_len = sizeof(ccc_val);
    descriptor.value = ccc_val;
    gatts_add_descriptor_sync(server_id, srvc_handle, &descriptor, &handle);
}

static uint8_t ble_uuid_add_service(void)
{
    sample_at_log_print("[uuid server] ble uuid add service in\r\n");
    bt_uuid_t service_uuid = {0};
    uint16_t handle = 0;
    stream_data_to_uuid(BLE_UUID_UUID_SERVER_SERVICE, &service_uuid);
    gatts_add_service_sync(BLE_UUID_SERVER_ID, &service_uuid, true, &handle);
    ble_uuid_server_add_rx_characters_and_descriptors(BLE_UUID_SERVER_ID, handle);
    ble_uuid_server_add_tx_characters_and_descriptors(BLE_UUID_SERVER_ID, handle);
    gatts_start_service(BLE_UUID_SERVER_ID, handle);
    sample_at_log_print("[uuid server] ble uuid add service out\r\n");
    return ERRCODE_BT_SUCCESS;
}

/* 服务删除回调 */
static void ble_uuid_server_service_delete_all_cbk(uint8_t server_id, errcode_t status)
{
    sample_at_log_print("[uuid server] delete all characters_and_descriptors cbk, service: %hhu, status: %d\n",
        server_id, status);
    g_service_cnt = 0;
    gatts_unregister_server(server_id);
}

/* 开始服务回调 */
static void ble_uuid_server_service_start_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    sample_at_log_print("[uuid server] start service cbk : server: %d status: %d srv_hdl: %d\n",
        server_id, status, handle);
    
    g_service_cnt++;
    if ((g_service_cnt == BLE_SERVICE_NUM) && (status == 0)) {
        sample_at_log_print("all service start end, start adv\n");
        ble_set_adv_data();
        ble_start_adv();
    }
}

/* 结束服务回调 */
static void ble_uuid_server_service_stop_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    sample_at_log_print("[uuid server] service stop cbk, service: %hhu, service_hdl: %hu, status: %d\n",
        server_id, handle, status);

    if (g_service_cnt > 0) {
        g_service_cnt--;
    }
    sample_at_log_print("service stop cbk, g_service_cnt: %hhu\n", g_service_cnt);

    if ((g_service_cnt == 0) && (status == 0)) {
        sample_at_log_print("all service stop end, start adv\n");
        gatts_delete_all_services(server_id);
    }
}

static void ble_uuid_server_receive_write_req_cbk(uint8_t server_id, uint16_t conn_id,
    gatts_req_write_cb_t *write_cb_para, errcode_t status)
{
    sample_at_log_print("[uuid server]ReceiveWriteReqCallback--server_id:%d conn_id:%d\n", server_id, conn_id);
    sample_at_log_print("request_id:%d att_handle:%d offset:%d need_rsp:%d need_authorize:%d is_prep:%d\n",
        write_cb_para->request_id, write_cb_para->handle, write_cb_para->offset, write_cb_para->need_rsp,
        write_cb_para->need_authorize, write_cb_para->is_prep);
    sample_at_log_print("data_len:%d data:\n", write_cb_para->length);
    for (uint8_t i = 0; i < write_cb_para->length; i++) {
        sample_at_log_print("%02x ", write_cb_para->value[i]);
    }
    sample_at_log_print("\n");
    sample_at_log_print("status:%d\n", status);
     
    // 将接收的数据重新发送回client
    uint8_t *response = malloc((write_cb_para->length + 1) * sizeof(uint8_t));
    memset_s(response, (write_cb_para->length + 1), 0, (write_cb_para->length + 1));
    memcpy_s(response, write_cb_para->length + 1, write_cb_para->value, write_cb_para->length);
    sample_at_log_print("ble write cbk, report[%s], len: %hu\n", response, write_cb_para->length);
 
    ble_uuid_server_send_report_by_handle(g_indication_characteristic_att_hdl, response, write_cb_para->length);
    
    free(response);
}

static void ble_uuid_server_receive_read_req_cbk(uint8_t server_id, uint16_t conn_id,
    gatts_req_read_cb_t *read_cb_para, errcode_t status)
{
    sample_at_log_print("[uuid server]ReceiveReadReq--server_id:%d conn_id:%d\n", server_id, conn_id);
    sample_at_log_print("request_id:%d att_handle:%d offset:%d need_rsp:%d need_authorize:%d is_long:%d\n",
        read_cb_para->request_id, read_cb_para->handle, read_cb_para->offset, read_cb_para->need_rsp,
        read_cb_para->need_authorize, read_cb_para->is_long);
    sample_at_log_print("status:%d\n", status);
}

static void ble_uuid_server_adv_enable_cbk(uint8_t adv_id, adv_status_t status)
{
    sample_at_log_print("adv enable adv_id: %d, status:%d\n", adv_id, status);
}

static void ble_uuid_server_adv_disable_cbk(uint8_t adv_id, adv_status_t status)
{
    sample_at_log_print("adv disable adv_id: %d, status:%d\n",
        adv_id, status);
}

void ble_uuid_server_connect_change_cbk(uint16_t conn_id, bd_addr_t *addr, gap_ble_conn_state_t conn_state,
    gap_ble_pair_state_t pair_state, gap_ble_disc_reason_t disc_reason)
{
    sample_at_log_print("connect state change conn_id: %d, status: %d, pair_status:%d, addr %x disc_reason %x\n",
        conn_id, conn_state, pair_state, addr[0], disc_reason);
    g_conn_hdl = conn_id;
    
    if (conn_state == GAP_BLE_STATE_DISCONNECTED) {
        if (g_connect_cnt > 0) {
            g_connect_cnt--;
        }
        if (disc_reason != GAP_BLE_CONN_TERMINATE_BY_LOCAL_HOST) {
            ble_set_adv_data();
            ble_start_adv();
        }
    } else if (conn_state == GAP_BLE_STATE_CONNECTED) {
        g_connect_cnt++;
    }
}

void ble_uuid_server_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id, uint16_t mtu_size, errcode_t status)
{
    sample_at_log_print("mtu change change server_id: %d, conn_id: %d, mtu_size: %d, status:%d \n",
        server_id, conn_id, mtu_size, status);
}

static void ble_uuid_server_enable_cbk(errcode_t status)
{
    sample_at_log_print("enable status: %d\n", status);
    
    uint8_t server_id = 0;
    bt_uuid_t app_uuid = {0};
    app_uuid.uuid_len = sizeof(g_uuid_app_uuid);
    if (memcpy_s(app_uuid.uuid, app_uuid.uuid_len, g_uuid_app_uuid, sizeof(g_uuid_app_uuid)) != EOK) {
        return;
    }
    gatts_register_server(&app_uuid, &server_id);
    ble_uuid_add_service();
    sample_at_log_print("[uuid server] init ok\r\n");
}

static void ble_uuid_server_disable_cbk(errcode_t status)
{
    sample_at_log_print("ble_disable cbk, status: %d\n", status);
}

static void ble_uuid_server_set_adv_data_cbk(uint8_t adv_id, errcode_t status)
{
    sample_at_log_print("set_adv_data cbk, adv_id: %hhu, status: %d\n", adv_id, status);
}

static void ble_uuid_server_set_adv_param_cbk(uint8_t adv_id, errcode_t status)
{
    sample_at_log_print("set_adv_param cbk, adv_id: %hhu, status: %d\n", adv_id, status);
}

static void ble_uuid_server_pair_result_cbk(uint16_t conn_id, const bd_addr_t *addr, errcode_t status)
{
    sample_at_log_print("pair result cbk, conn_id: %hu, status: %d, addr[%02X:%02X:%02X:%02X:%02X:%02X]\n",
        conn_id, status, addr->addr[0], addr->addr[1], addr->addr[2], addr->addr[3], addr->addr[4], addr->addr[5]);
}

static void ble_uuid_server_adv_terminate_cbk(uint8_t adv_id, adv_status_t status)
{
    sample_at_log_print("adv terminate cbk, adv_id: %hhu, status: %d\n", adv_id, status);
}

static errcode_t ble_dev_manager_callbacks(void)
{
    bts_dev_manager_callbacks_t dev_mgr = {0};

    dev_mgr.ble_enable_cb = ble_uuid_server_enable_cbk;
    dev_mgr.ble_disable_cb = ble_uuid_server_disable_cbk;

    return bts_dev_manager_register_callbacks(&dev_mgr);
}

static errcode_t ble_uuid_server_register_callbacks(void)
{
    errcode_t ret;

    ret = ble_dev_manager_callbacks();
    if (ret != ERRCODE_BT_SUCCESS) {
        sample_at_log_print("[uuid server] reg dev manager cbk failed.\r\n");
        return ERRCODE_BT_FAIL;
    }

    gap_ble_callbacks_t gap_cb = {0};
    gatts_callbacks_t service_cb = {0};

    gap_cb.set_adv_data_cb = ble_uuid_server_set_adv_data_cbk;
    gap_cb.set_adv_param_cb = ble_uuid_server_set_adv_param_cbk;
    gap_cb.start_adv_cb = ble_uuid_server_adv_enable_cbk;
    gap_cb.stop_adv_cb = ble_uuid_server_adv_disable_cbk;
    gap_cb.conn_state_change_cb = ble_uuid_server_connect_change_cbk;
    gap_cb.pair_result_cb = ble_uuid_server_pair_result_cbk;
    gap_cb.terminate_adv_cb = ble_uuid_server_adv_terminate_cbk;
    ret = gap_ble_register_callbacks(&gap_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        sample_at_log_print("[uuid server] reg gap cbk failed\r\n");
        return ERRCODE_BT_FAIL;
    }

    service_cb.start_service_cb = ble_uuid_server_service_start_cbk;
    service_cb.stop_service_cb = ble_uuid_server_service_stop_cbk;
    service_cb.delete_service_cb = ble_uuid_server_service_delete_all_cbk;
    service_cb.read_request_cb = ble_uuid_server_receive_read_req_cbk;
    service_cb.write_request_cb = ble_uuid_server_receive_write_req_cbk;
    service_cb.mtu_changed_cb = ble_uuid_server_mtu_changed_cbk;
    ret = gatts_register_callbacks(&service_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        sample_at_log_print("[uuid server] reg service cbk failed\r\n");
        return ERRCODE_BT_FAIL;
    }
    return ret;
}

/* 初始化uuid server service */
errcode_t ble_uuid_server_init(void)
{
    ble_uuid_server_register_callbacks();

    errcode_t ret = enable_ble();
    if (ret != ERRCODE_BT_SUCCESS) {
        sample_at_log_print("[uuid server] ble_server_init, enable_ble fail, ret = %d\n", ret);
        return ERRCODE_BT_FAIL;
    }

    return ERRCODE_BT_SUCCESS;
}

/* 反初始化uuid server service */
errcode_t ble_uuid_server_deinit(void)
{
    sample_at_log_print("[uuid server] ble_uuid_server_deinit");

    gap_ble_remove_all_pairs();

    while (g_connect_cnt) {
        usleep(BLE_SLEEP_US);
    }
    disable_ble();
    return ERRCODE_BT_SUCCESS;
}

/* device通过uuid向host发送数据：report */
errcode_t ble_uuid_server_send_report_by_uuid(const uint8_t *data, uint8_t len)
{
    gatts_ntf_ind_by_uuid_t param = {0};
    uint16_t conn_id = g_conn_hdl;
    param.start_handle = 0;
    param.end_handle = 0xffff;
    stream_data_to_uuid(BLE_UUID_UUID_SERVER_REPORT, &param.chara_uuid);
    param.value_len = len;
    param.value = osal_vmalloc(len);
    if (param.value == NULL) {
        sample_at_log_print("[hid][ERROR]send report new fail\r\n");
        return ERRCODE_BT_FAIL;
    }
    if (memcpy_s(param.value, param.value_len, data, len) != EOK) {
        sample_at_log_print("[hid][ERROR]send input report memcpy fail\r\n");
        osal_vfree(param.value);
        return ERRCODE_BT_FAIL;
    }
    gatts_notify_indicate_by_uuid(BLE_UUID_SERVER_ID, conn_id, &param);
    osal_vfree(param.value);
    return ERRCODE_BT_SUCCESS;
}

/* device通过handle向host发送数据：report */
errcode_t ble_uuid_server_send_report_by_handle(uint16_t attr_handle, const uint8_t *data, uint8_t len)
{
    gatts_ntf_ind_t param = {0};
    uint16_t conn_id = g_conn_hdl;

    param.attr_handle = attr_handle;
    param.value = osal_vmalloc(len);
    param.value_len = len;

    if (param.value == NULL) {
        sample_at_log_print("[hid][ERROR]send report new fail\r\n");
        return ERRCODE_BT_FAIL;
    }
    if (memcpy_s(param.value, param.value_len, data, len) != EOK) {
        sample_at_log_print("[hid][ERROR]send input report memcpy fail\r\n");
        osal_vfree(param.value);
        return ERRCODE_BT_FAIL;
    }
    gatts_notify_indicate(BLE_UUID_SERVER_ID, conn_id, &param);
    osal_vfree(param.value);
    return ERRCODE_BT_SUCCESS;
}

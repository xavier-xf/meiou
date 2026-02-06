/*
 * Copyright (c) @CompanyNameMagicTag 2023-2023. All rights reserved.
 */

#include <linux/version.h>

#include "hcc_comm.h"
#include "hcc_if.h"
#include "hcc_adapt.h"
#include "sle_chan_kernel.h"
#include "sle_hci_chba_proc.h"
#include "soc_osal.h"
#include "td_base.h"
#include "oal_schedule.h"
#include "oal_types.h"
#include "sle_hcc_proc.h"

static uint8_t g_sle_state = SLE_OFF;

#define SLE_DEVICE_OPEN_CLOSE_MSG_FINISH_MAX_WAIT_TIME 2000
#define SLE_DEVICE_OPEN_CLOSE_MSG_FINISH_WAIT_TIME 10

#define SLE_OPEN_BTC_FINISH 1
#define SLE_CLOSE_BTC_FINISH 0

#define HCI_DATATYPE_GLE_OPEN_BTC_MSG 0xA5
#define HCI_DATATYPE_GLE_CLOSE_BTC_MSG 0xA6
static int sle_open(void);
static int sle_close(void);
static int sle_send_ctrl_btc_msg(uint8_t type);
static void sle_open_close_btc_finish_handle(osal_u8 event);

static int sle_send_ctrl_btc_msg(uint8_t type)
{
    switch (type) {
        case HCI_DATATYPE_GLE_OPEN_BTC_MSG: {
            hcc_debug("sle send ctrl btc msg open enter\n");
            return sle_open();
        }
        case HCI_DATATYPE_GLE_CLOSE_BTC_MSG: {
            hcc_debug("sle send ctrl btc msg close enter\n");
            return sle_close();
        }
        default:
            break;
    }
    return EXT_ERR_FAILURE;
}
 
static int sle_open(void)
{
    oal_uint64 start = oal_get_time_stamp_from_timeval();
    oal_uint64 end = 0;
    int ret;
    if (g_sle_state == SLE_ON) {
        hcc_debug("sle device already open \n");
        return EXT_ERR_SUCCESS;
    }
    pm_sle_enable_reply_cb_host_register(sle_open_close_btc_finish_handle);
    ret = pm_sle_enable();
    if (ret != OAL_SUCC) {
        hcc_debug("sle send open btc msg fail ret 0x%x\n", ret);
        return EXT_ERR_SUCCESS;
    }

    while (g_sle_state == SLE_OFF) {
        osal_msleep(SLE_DEVICE_OPEN_CLOSE_MSG_FINISH_WAIT_TIME);
        end = oal_get_time_stamp_from_timeval();
        if (end >= start + SLE_DEVICE_OPEN_CLOSE_MSG_FINISH_MAX_WAIT_TIME) {
            hcc_debug("sle open device finish fail ,time out \n");
            return EXT_ERR_SUCCESS;
        }
    }
    end = oal_get_time_stamp_from_timeval();
    g_sle_state = SLE_ON;
    hcc_debug(" sle open time:%lld \n", end - start);
    return EXT_ERR_SUCCESS;
}
 
static int sle_close(void)
{
    oal_uint64 start = oal_get_time_stamp_from_timeval();
    oal_uint64 end = 0;
    int ret;
    if (g_sle_state == SLE_OFF) {
        hcc_debug("sle device already closed \n");
        return EXT_ERR_SUCCESS;
    }
    ret = pm_sle_disable();
    if (ret != OAL_SUCC) {
        hcc_debug("sle send close btc msg fail ret 0x%x\n", ret);
        return EXT_ERR_SUCCESS;
    }

    while (g_sle_state == SLE_ON) {
        osal_msleep(SLE_DEVICE_OPEN_CLOSE_MSG_FINISH_WAIT_TIME);
        end = oal_get_time_stamp_from_timeval();
        if (end >= start + SLE_DEVICE_OPEN_CLOSE_MSG_FINISH_MAX_WAIT_TIME) {
            hcc_debug("sle_close device finish fail ,time out \n");
            sle_hci_register_send_hcc_interface(NULL);
            g_sle_state = SLE_OFF;
            return EXT_ERR_SUCCESS;
        }
    }
    end = oal_get_time_stamp_from_timeval();
    g_sle_state = SLE_OFF;
    sle_hci_register_send_hcc_interface(NULL);
    hcc_debug(" sle_close time:%lld \n", end - start);
    return EXT_ERR_SUCCESS;
}

static void sle_open_close_btc_finish_handle(osal_u8 event)
{
    switch (event) {
        case SLE_OPEN_BTC_FINISH: {
            hcc_debug(" sle btc open finish \n");
            g_sle_state = SLE_ON;
            break;
        }
        case SLE_CLOSE_BTC_FINISH: {
            hcc_debug(" sle btc close finish \n");
            g_sle_state = SLE_OFF;
            break;
        }
        default:
            break;
    }
    return;
}

void set_sle_state(uint8_t sle_state)
{
    hcc_debug("sle set sle state %d\n", sle_state);
    g_sle_state = sle_state;
}

/**
  * @function : sle_hcc_adapt_free
  * @brief    : liteos的bsle host向device发送的消息,hcc需要额外分配sdio传输相应的32字节对齐的内存空间,
  * @brief    : 在hcc发送消息成功后,会进入此回调函数
  * @param    : queue_id [input] hcc通道收发消息类型id
  * @param    : buf [input]  需要被释放的hcc通道申请的内存空间
  * @param    : user_param [input]  需要被释放的bth申请的内存空间
  * @return   : void
  */
static td_void sle_hcc_adapt_free(hcc_queue_type queue_id, td_u8 *buf, td_u8 *user_param)
{
    if (user_param != TD_NULL) {
        osal_kfree(user_param);
    }
    if (buf != TD_NULL) {
        osal_kfree(buf);
    }
}

/**
  * @function : sle_hcc_adapt_alloc
  * @brief    : bsle device向liteos发送的消息,hcc接收到原始消息需要申请32字节对齐的内存空间
  * @param    : queue_id [input]  hcc通道收发消息类型id
  * @param    : len [input]  hcc申请保存数据的大小
  * @param    : buf [input]  hcc申请保存数据的内存空间地址
  * @param    : user_param [input]  当前未使用,业务可能需要的额外内存空间可以指向这里
  * @return   : 内存分配正常返回EXT_ERR_SUCCESS
  */
static td_u32 sle_hcc_adapt_alloc(hcc_queue_type queue_id, td_u32 len, td_u8 **buf, td_u8 **user_param)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
    *buf = (td_u8 *)osal_kmalloc(len, OSAL_GFP_ATOMIC);
#else
    *buf = (td_u8 *)osal_kmalloc(len, OSAL_GFP_KERNEL);
#endif
    if (*buf == TD_NULL) {
        hcc_debug("sle_hcc_adapt_alloc failed\r\n");
        return EXT_ERR_FAILURE;
    }
    return EXT_ERR_SUCCESS;
}

/**
  * @function : sle_hcc_adapt_rx
  * @brief    : bsle host的rx处理,user_param和buf共享同一片内存空间,user_param即是bsle_hcc_adapt_alloc函数中的buf
  * @param    : queue_id [input]  hcc通道收发消息类型id
  * @param    : len [input]  device发送消息的实际长度
  * @param    : buf [input]  device发送的实际消息的内存起始地址
  * @param    : user_param [input]  指向的是hcc原始分配的32字节对齐的起始内存空间地址
  * @return   : 接收业务处理正常返回EXT_ERR_SUCCESS
  */
static td_u32 sle_hcc_adapt_rx(hcc_queue_type queue_id, td_u8 sub_type, td_u8 *buf, td_u32 len, td_u8 *user_param)
{
    if (buf == TD_NULL || user_param == TD_NULL) {
        return EXT_ERR_FAILURE;
    }
    if (g_sle_state == SLE_OFF) {
        osal_kfree(user_param);
        hcc_debug("sle hcc adapt rx sle state %d\n", g_sle_state);
        return EXT_ERR_FAILURE;
    }

    if (sle_hci_send_to_chba(buf, len) == 0) {
        sle_hci_chan_recv_frame(buf, len);
    }

    osal_kfree(user_param);
    return EXT_ERR_SUCCESS;
}

hcc_adapt_ops g_sle_hcc_adapt = {
    .free = sle_hcc_adapt_free,
    .alloc = sle_hcc_adapt_alloc,
    .start_subq = TD_NULL,
    .stop_subq = TD_NULL,
    .rx_proc = sle_hcc_adapt_rx,
};

/* Send frames from HCI layer */
void sle_hci_send_frame(uint8_t *data_buf, uint16_t len)
{
    int ret;

    /* 已和平台确认，sub_type和fc_flag值为0的话，则不生效 */
    hcc_transfer_param sle_transfer_param = {0};

    if (sle_send_ctrl_btc_msg(data_buf[0]) == EXT_ERR_SUCCESS) {
        hcc_debug("sle hci send frame open/close msg\n");
        return;
    }

    if (g_sle_state == SLE_OFF) {
        hcc_debug("sle hci send frame sle state closed\n");
        return;
    }

    sle_transfer_param.service_type = HCC_ACTION_TYPE_SLE;
    /* sub_type类型业务可以在service_type上自定义，在device侧去区分做不同业务 */
    sle_transfer_param.sub_type = 0;
    sle_transfer_param.queue_id = SLE_DATA_QUEUE;
    /* fc_flag代表流控类型，当前蓝牙未使用 */
    sle_transfer_param.fc_flag = 0;
    sle_transfer_param.user_param = NULL;

    /**
     * 调用完tx_data后,若hcc返回成功,则进入到hcc_sle_rx_proc释放hcc通道申请的buf。
     * 因为netlink机制,此处不用释放data_buf.
     */
    ret = hcc_bt_tx_data(HCC_CHANNEL_AP, data_buf, len, &sle_transfer_param);
    if (ret != EXT_ERR_SUCCESS) {
        hcc_debug("send tx data failed\n");
    }
}

ext_errno sle_hcc_init(void)
{
    sle_hci_register_send_hcc_interface(sle_hci_send_frame);
    g_sle_state = SLE_OFF;
    return hcc_service_init(HCC_CHANNEL_AP, HCC_ACTION_TYPE_SLE, &g_sle_hcc_adapt);
}

ext_errno sle_hcc_deinit(void)
{
    hcc_service_deinit(HCC_CHANNEL_AP, HCC_ACTION_TYPE_SLE);
    pm_sle_enable_reply_cb_host_register((pm_bsle_enable_reply_cb)NULL);
    g_sle_state = SLE_OFF;
    return EXT_ERR_SUCCESS;
}
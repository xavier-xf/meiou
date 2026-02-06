/*
 * Copyright (c) CompanyNameMagicTag 2020-2020. All rights reserved.
 */
#include "sle_host_register.h"
#include "sle_chan_kernel.h"
#include "sle_tm_chba_proc.h"
#include "sle_hcc_proc.h"

#include <asm/unaligned.h>

MODULE_LICENSE("GPL");

#define SLE_DFR_EVENT_DATA_LEN 7
static uint8_t sle_dfr_event[SLE_DFR_EVENT_DATA_LEN] = {0xa2, 0xaa, 0xfc, 0x02, 0x00, 0x00, 0x00};

static int32_t sle_recovery(void);
static int32_t sle_dfr_recovery(void);
static osal_void sle_stop(void);

static void sle_send_dfr_event_to_host(void)
{
    hcc_debug("sle send dfr event to host enter\n");
    sle_hci_chan_recv_frame(sle_dfr_event, SLE_DFR_EVENT_DATA_LEN);
}

static int32_t sle_host_init(void)
{
    int32_t ret;
    bool boot_finish;

    ret = pm_sle_open();
    if (ret != OAL_SUCC) {
        oal_print_err("pm_sle_open failed\n");
    } else {
        oal_print_err("pm_sle_open success\n");
    }

    boot_finish = hbsle_hcc_customize_get_device_status(BSLE_STATUS_BOOT_FINISH);
    if (boot_finish == false) {
        hcc_debug("device boot not finish \n");
    }

    oal_print_err("sle device open success\n");

    ret = sle_hcc_init();
    if (ret != OAL_SUCC) {
        hcc_debug("initial hcc sle service failed\n");
        return OAL_FAIL;
    }

    ret = sle_chan_init();
    if (ret == SLE_CHAN_FAILED) {
        oal_print_err("sle_chan_init failed\r\n");
        return OAL_FAIL;
    }
    sle_hci_register_send_frame_cb((sle_send_frame_cb_t)sle_hci_send_frame);
    sle_tm_register_send_frame_cb((sle_send_frame_cb_t)sle_tm_send_to_chba);
    hci_kernel_init();
    // 注册suspend回调
    pm_sle_suspend_cb_host_register(sle_stop);
    // 注册resume回调
    pm_sle_resume_cb_host_register(sle_recovery);
    // 注册dfr回调
    plat_sle_exception_rst_register_etc(sle_dfr_recovery);
    set_sle_state(SLE_OFF);
    oal_print_info("sle host init finished\n");
    return OAL_SUCC;
}

static osal_void sle_host_deinit(void)
{
    int32_t ret;
    oal_print_err("enter:%s\n", __func__);
    pm_sle_suspend_cb_host_register(NULL);
    pm_sle_resume_cb_host_register(NULL);
    ret = pm_sle_close();
    if (ret != OAL_SUCC) {
        oal_print_err("pm_sle_close failed\n");
    } else {
        oal_print_info("finish: pm_sle_close\n");
    }

    hbsle_hcc_customize_reset_device_status();
    sle_chan_exit();
    sle_hcc_deinit();
    set_sle_state(SLE_OFF);
    oal_print_info("finish: H2D_MSG_SLE_CLOSE\n");
}

static osal_void sle_stop(void)
{
    set_sle_state(SLE_OFF);
}

static int32_t sle_recovery(void)
{
    int32_t ret;
    bool boot_finish;
    bool customize_received;
    hcc_debug("start sle recovery \n");
    /* 恢复HCC状态 */
    hcc_switch_status(HCC_ON);
    hbsle_hcc_customize_reset_device_status();
    /* 重新下发定制化配置 */
    ret = hbsle_hcc_custom_ini_data_buf();
    if (ret != OAL_SUCC) {
        hcc_debug("sle recovery::hcc ini data,fail ret=%d. \n", ret);
        return ret;
    }
    customize_received = hbsle_hcc_customize_get_device_status(BSLE_STATUS_CUSTOMIZE_RECEIVED);
    if (customize_received == false) {
        hcc_debug("customize data not received \n");
    }
    ret = hcc_send_message(HCC_CHANNEL_AP, H2D_MSG_SLE_OPEN, HCC_ACTION_TYPE_TEST);
    if (ret != OAL_SUCC) {
        hcc_debug("sle recovery::ble/sle open,fail, ret=%d. \n", ret);
        return ret;
    }
    boot_finish = hbsle_hcc_customize_get_device_status(BSLE_STATUS_BOOT_FINISH);
    if (boot_finish == false) {
        hcc_debug("device boot not finish \n");
    }
    set_sle_state(SLE_OFF);
    sle_send_dfr_event_to_host();
    return ret;
}

static int32_t sle_dfr_recovery(void)
{
    int32_t ret;
    hcc_debug("sle_dfr_recovery start \r\n");
    ret = sle_recovery();
    if (ret != OAL_SUCC) {
        oal_print_err("sle dfr recovery error\n");
    }
    return ret;
}

module_init(sle_host_init);
module_exit(sle_host_deinit);
EXPORT_SYMBOL(sle_tm_register_chba_recv_interface);
EXPORT_SYMBOL(sle_hci_register_chba_recv_interface);
EXPORT_SYMBOL(sle_hci_recv_from_chba);

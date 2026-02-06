/*
 * Copyright (c) @CompanyNameMagicTag 2023-2023. All rights reserved.
 */

#ifndef SLE_HCC_PROC_H
#define SLE_HCC_PROC_H

#include "td_base.h"

#ifndef SLE_ON
#define SLE_ON 1
#endif
#ifndef SLE_OFF
#define SLE_OFF 0
#endif

ext_errno sle_hcc_init(void);
ext_errno sle_hcc_deinit(void);
void sle_hci_send_frame(uint8_t *data_buf, uint16_t len);
void set_sle_state(uint8_t sle_state);
typedef void (*pm_bsle_enable_reply_cb)(osal_u8);
extern osal_void pm_sle_enable_reply_cb_host_register(pm_bsle_enable_reply_cb cb);
extern osal_s32 pm_sle_enable(void);
extern osal_s32 pm_sle_disable(void);
#endif
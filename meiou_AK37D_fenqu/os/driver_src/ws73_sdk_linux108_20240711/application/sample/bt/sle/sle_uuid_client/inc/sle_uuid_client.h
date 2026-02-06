/*
 * Copyright (c) @CompanyNameMagicTag 2022. All rights reserved.
 *
 * Description: SLE private service register sample of client.
 */

/**
 * @defgroup SLE UUID CLIENT API
 * @ingroup
 * @{
 */

#ifndef SLE_CLIENT_ADV_H
#define SLE_CLIENT_ADV_H

typedef enum {
    STATE_IDLE          = 0,
    STATE_SEEKING       = 1,
    STATE_CONNECTING    = 2,
    STATE_PAIRING       = 3,
    STATE_EXCHANING     = 4,
    STATE_DISCOVERING   = 5,
    STATE_TRANSFER      = 6,
    STATE_MAX           = 7,
} sle_state_t;

typedef enum {
    EVENT_SCAN_TRIG         = 0x00,
    EVENT_CONNECT_TRIG      = 0x01,
    EVENT_SCAN_CMPL         = 0x02,
    EVENT_SCAN_FAIL         = 0x03,
    EVENT_CONNECT_CMPL      = 0x04,
    EVENT_CONNECT_FAIL      = 0x05,
    EVENT_PAIR_CMPL         = 0x06,
    EVENT_PAIR_FAIL         = 0x07,
    EVENT_EXCHANGE_CMPL     = 0x08,
    EVENT_EXCHANGE_FAIL     = 0x09,
    EVENT_DISCOVER_CMPL     = 0x0a,
    EVENT_DISCOVER_FAIL     = 0x0b,
    EVENT_STATE_CHANGE      = 0x0c,
    EVENT_DISCONNECT        = 0x0d,
    EVENT_MAX               = 0x0e,
} sle_event_t;

typedef struct {
    sle_addr_t          addr;
    struct DLIST_NODE   list;
} sle_scan_result_t;

typedef struct {
    sle_addr_t                  addr;
    uint16_t                    conn_id;
    ssapc_find_service_result_t service;
    struct DLIST_NODE           list;
} sle_peri_t;

typedef struct {
    struct DLIST_NODE   sle_scan_result;
    struct DLIST_NODE   sle_peri;
    sle_peri_t          *sle_target_peri;
    sle_state_t         state;
    int                 mq;
    pthread_t           state_machine_thread;
    int                 state_machine_quit;
} sle_central_t;

struct st_event {
    long        mtype;      /* message type, must be > 0 */
    sle_event_t event;      /* message data */
};

extern sle_central_t g_sle_central;
/**
 * @if Eng
 * @brief  sle uuid client init.
 * @attention  NULL
 * @retval ERRCODE_SLE_SUCCESS    Excute successfully
 * @retval ERRCODE_SLE_FAIL       Execute fail
 * @par Dependency:
 * @li NULL
 * @else
 * @brief  sle uuid客户端初始化。
 * @attention  NULL
 * @retval ERRCODE_SLE_SUCCESS    执行成功
 * @retval ERRCODE_SLE_FAIL       执行失败
 * @par 依赖:
 * @li NULL
 * @endif
 */
void sle_client_init(void);

/**
 * @if Eng
 * @brief  sle start scan.
 * @attention  NULL
 * @retval ERRCODE_SLE_SUCCESS    Excute successfully
 * @retval ERRCODE_SLE_FAIL       Execute fail
 * @par Dependency:
 * @li NULL
 * @else
 * @brief  sle启动扫描。
 * @attention  NULL
 * @retval ERRCODE_SLE_SUCCESS    执行成功
 * @retval ERRCODE_SLE_FAIL       执行失败
 * @par 依赖:
 * @li NULL
 * @endif
 */
void sle_start_scan(void);

void sle_stop_scan(void);

sle_peri_t *sle_connect(sle_addr_t *addr);

int sle_write_req(sle_peri_t *peri, uint8_t *data, uint8_t len);
int sle_comm_init();
void sle_comm_deinit();
void sle_recv_ack(void);
int sle_write_msgq(uint8_t *data, uint8_t len);
int sle_write_sync(sle_peri_t *peri, uint8_t *data, uint8_t len, uint16_t conn_intval);
int sle_change_phy_2M(uint16_t handle);

#endif
#ifndef _AK_DRV_TS_H_
#define _AK_DRV_TS_H_


#define TS_POINT_MAX_NUM	10

struct ak_ts_info{
    int x;
    int y;
};

struct ak_ts_event{
    int map;
    struct ak_ts_info info[TS_POINT_MAX_NUM];
};


/**
 * ak_drv_ts_open: open touch screen driver
 * return: 0 - success; otherwise error code;
 */
int ak_drv_ts_open(void);

/**
 * ak_drv_ts_get_event: get touch screen event .
 * @ts[OUT]:  ts event
 * @ms[IN]:  time out , <0  block; = 0  unblock; > 0 wait time.
 * return: 0 - success; otherwise error code;
 */
int ak_drv_ts_get_event(struct ak_ts_event *ts, long ms);

/**
 * ak_drv_ts_close: close touch screen .
 * return: 0 - success; otherwise error code;
 */
int ak_drv_ts_close(void);


#endif

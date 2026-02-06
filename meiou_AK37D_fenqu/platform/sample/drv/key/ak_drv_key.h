#ifndef _AK_DRV_KEY_H_
#define _AK_DRV_KEY_H_


enum key_stat{
    PRESS = 1,
    RELEASE,
};

struct key_event{
    int code;
    enum key_stat stat;
};

/**
 * ak_drv_key_open: open key driver
 * return: 0 - success; otherwise error code;
 */
int ak_drv_key_open(void);

/**
 * ak_drv_key_get_event: get key event .
 * @key[OUT]:  key event
 * @ms[IN]:  time out , <0  block; = 0  unblock; > 0 wait time.
 * return: 0 - success; otherwise error code;
 */
int ak_drv_key_get_event(struct key_event *key, long ms);

int ak_drv_key_get_more(struct key_event *key);

/**
 * ak_drv_key_close: close key .
 * @handle[IN]:  key handle
 * return: 0 - success; otherwise error;
 */
int ak_drv_key_close(void);

#endif

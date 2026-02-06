#ifndef _AK_DRV_PWM_H_
#define _AK_DRV_PWM_H_


/* 
 * ak_drv_pwm_open - open pwm device
 * @device_no[IN]: pwm device minor-number,[0-4]
 * return: 0 - success; otherwise error code;
 */
int ak_drv_pwm_open(int device_no);

/* 
 * ak_drv_pwm_set - set pwd working param
 * @device_no[IN]: pwm device minor-number,[0-4]
 * @duty_ns[IN]: pwm duty time in ns.
 * @period_ns[IN]: pwm period time in ns.
 * return: 0 on success, otherwise error code.
 */
int ak_drv_pwm_set(int device_no, int duty_ns, int period_ns);

/* 
 * ak_drv_pwm_close - close pwm
 * @device_no[IN]: pwm device minor-number,[0-4]
 * return: 0 on success, otherwise error code.
 */
int ak_drv_pwm_close(int device_no);


#endif

/*
 * f22_f23_f28_f35_f37 Camera Driver
 *
 * Copyright anyka
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-image-sizes.h>
#include "../include/ak_video_priv_cmd.h"
#include "../include_internal/ak_video_priv_cmd_internal.h"
#include "../include/ak_isp_drv.h"

#include "../include/ak_sensor.h"
#include "../include/sensor_cmd.h"
#include "../common/sensor_sys.h"
#include "../common/sensor_i2c.h"

/*
 *
SENSOR_PWDN_LEVEL:	the io level when sensor power down
SENSOR_RESET_LEVEL:	the io level when sensor reset
SENSOR_I2C_ADDR:	the sensor slave address, 7 bits width.
the i2c bus left shift 1bits.
SENSOR_ID:	the sensor ID.
SENSOR_MCLK:	mclk, unit：MHz。24MHz or 27MHz mclk for most sensors.
NOTE: 27MHz mclk is inaccurate.
SENSOR_REGADDR_BYTE:	sensor register address bytes width, 1 byte or 2 bytes.
SENSOR_DATA_BYTE:		register's value bytes width, 1 byte or 2bytes.
SENSOR_OUTPUT_WIDTH:	use for isp cut window MAX width.
SENSOR_OUTPUT_HEIGHT:	use for isp cut window MAX height.
SENSOR_VALID_OFFSET_X:	use for isp cut window left offset.
SENSOR_VALID_OFFSET_Y:	use for isp cut window top offset.
SENSOR_BUS_TYPE:		sensor output data type.
SENSOR_IO_INTERFACE:	sensor output data bus type.
SENSOR_IO_LEVEL:		sensor output data io level.
MAX_FPS:				current sensor configurtion support max fps.
EXP_EFFECT_FRAMES:		!!IMPORT!! frequency for update exp_time
		0 - adjust exp_time for every frames
		1 - adjust exp_time for 1/2 frames
		2 - adjust exp_time for 1/3 frames
		and so on.
A_GAIN_EFFECT_FRAMES:	!!IMPORT!! frequency for update a_gain
		0 - adjust a_gain for every frames
		1 - adjust a_gain for 1/2 frames
		2 - adjust a_gain for 1/3 frames
		and so on.
D_GAIN_EFFECT_FRAMES:	!!IMPORT!! frequency for update d_gain
		0 - adjust d_gain for every frames
		1 - adjust d_gain for 1/2 frames
		2 - adjust d_gain for 1/3 frames
		and so on.
DELAY_FLAG
 *
 * */
#define SENSOR_PWDN_LEVEL		0
#define SENSOR_RESET_LEVEL		0
#define SENSOR_I2C_ADDR			0x40
#define SENSOR_ID_F22           (0x0f22)
#define SENSOR_ID_F23           (0x0f23)
#define SENSOR_ID_F28           (0x0f28)
#define SENSOR_ID_F35           (0x0f35)
#define SENSOR_ID_F37           (0x0f37)
#define SENSOR_ID_F37P          (0x0841)
static int SENSOR_ID = 0;
#define SENSOR_MCLK				24
#define SENSOR_REGADDR_BYTE		1
#define SENSOR_DATA_BYTE		1
#define SENSOR_OUTPUT_WIDTH		1920
#define SENSOR_OUTPUT_HEIGHT	1080
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_RAW
//#define SENSOR_IO_INTERFACE		DVP_INTERFACE
#define SENSOR_IO_INTERFACE		MIPI_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_1V8
#define MIPI_MBPS				720	/*3xx up to 4xx*/
static int MIPI_LANES			= 1;
static int MAX_FPS				= 25;
#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	0	/*adjust a_gain for every frames*/
#define D_GAIN_EFFECT_FRAMES	0	/*adjust d_gain for every frames*/

#define DELAY_FLAG		(0xffff)
#define OV_MAX(a, b)            (((a) < (b) ) ?  (b) : (a))
#define OV_MIN(a, b)            (((a) > (b) ) ?  (b) : (a))
#define OV_CLIP3(low, high, x)  (OV_MAX(OV_MIN((x), high), low))

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct jxfxx_win_size {
	char				*name;
	u32				width;
	u32				height;
	const struct regval_list	*regs;
};

/*
 * store sensor fps informations
 * @current_fps: current fps
 * @to_fps: should changed to the fps
 * @to_fps_value: the fps paramter
 */
struct sensor_fps_info {
	int current_fps;
	int to_fps;
	int to_fps_value;
};

/*
 * the host callbacks for sensor
 * @isp_timing_cb_info: isp timing callback.
 * 	a few sensor need the callback when fps had changed
 */
struct host_callbacks {
	struct isp_timing_cb_info isp_timing_cb_info;
};

/*
 * aec paramters. some sensors need record paramters.
 */
struct jxfxx_aec_parms {
};

static const struct v4l2_ctrl_ops jxfxx_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &jxfxx_ctrl_ops,
	.id		= SENSOR_GET_ID,
	.name	= "get sensor id",
	.type	= V4L2_CTRL_TYPE_INTEGER,
	.min	= 0,
	.max	= 0xfffffff,
	.step	= 1,
	.def	= 0,
	.flags	= V4L2_CTRL_FLAG_VOLATILE,/*set V4L2_CTRL_FLAG_VOLATILE avoid get cached*/
};

/*
 * the sensor struct
 * @client: point to i2c client
 * @subdev: v4l2 subdev struct
 * @hdl: 	v4l2 control handler
 * @cfmt_code:
 * @win:	default window
 * @gpio_reset:	gpio number for reset pin
 * @gpio_pwdn:	gpio number for power down pin
 * @cb_info:	sensor callbacks
 * @fps_info:	fps informations
 * @hcb:		host callbacks
 * @aec_parms:	aec paramters
 */
struct jxfxx_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct jxfxx_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct jxfxx_aec_parms aec_parms;

	AK_ISP_SENSOR_INIT_PARA para;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};
static struct jxfxx_priv *g_jxfxx_priv = NULL;

static AK_ISP_SENSOR_CB jxfxx_cb;

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 1;
module_param(check_id, int, 0644);

static void jxfxx_set_fps_async(struct jxfxx_priv *priv);
static int __jxfxx_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int jxfxx_sensor_read_id_func(void *arg);   //no use IIC bus
static int jxfxx_sensor_get_resolution_func(void *arg, int *width, int *height);
static int jxfxx_sensor_set_fps_func(void *arg, const int fps);
static int jxfxx_fps_to_vts(struct jxfxx_priv *priv, const int fps);
static int jxfxx_sensor_probe_id_func(void *arg);  //use IIC bus
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 jxfxx_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct jxfxx_priv *to_jxfxx(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct jxfxx_priv,
			subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct jxfxx_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int jxfxx_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int jxfxx_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	int ret = 0;

	switch (ctrl->id) {
		case SENSOR_GET_ID:
			ctrl->val = SENSOR_ID;
			break;

		default:
			ret = -EINVAL;
			pr_err("%s id:%d no support\n", __func__, ctrl->id);
			break;
	}

	return ret;
}

/*
 * XXXX_s_stream -
 * set ctrls
 * soc_camera_ops functions
 *
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	return 0;
}

/*
 * XXXX_s_power -
 * set power operation
 * soc_camera_ops functions
 *
 * @sd:			subdev
 * @on:			power flags
 */
static int jxfxx_core_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}

/*
 * XXXX_get_sensor_id -
 * get sensor ID
 * private callback functions
 *
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = jxfxx_sensor_read_id_func(NULL);   //no use IIC bus
	return 0;
}

/*
 * XXXX_get_sensor_id -
 * get sensor callback
 * private callback functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jxfxx_priv *priv = to_jxfxx(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int jxfxx_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jxfxx_priv *priv = to_jxfxx(client);
	int fps = ctrl->value;
	int vts = jxfxx_fps_to_vts(priv, fps);

	ctrl->value = vts - 4;
	return 0;
}

/*
 * XXXX_core_g_ctrl -
 * get control
 * core functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = jxfxx_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = jxfxx_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = jxfxx_get_max_exp_for_fps(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

/*
 * XXXX_set_isp_timing_cb -
 * set isp timing callback function
 * private callback functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jxfxx_priv *priv = to_jxfxx(client);
	struct isp_timing_cb_info *isp_timing_cb_info = (void *)ctrl->value;

	memcpy(&priv->hcb.isp_timing_cb_info,
			isp_timing_cb_info,
			sizeof(struct isp_timing_cb_info));
	return 0;
}

/*
 * XXXX_set_fps_direct -
 * set sensor fps directly, synchronously.
 * private callback functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jxfxx_priv *priv = to_jxfxx(client);
	int fps = ctrl->value;

	jxfxx_sensor_set_fps_func(priv, fps);
	jxfxx_set_fps_async(priv);
	return 0;
}

/*
 * XXXX_core_s_ctrl -
 * set core s_ctrl
 * core functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int jxfxx_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = jxfxx_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = jxfxx_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int jxfxx_store_initial_regs(struct jxfxx_priv *priv, void *arg)
{
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *sensor_para = arg;
	void *p_reg_info;
	int ret;

	p_reg_info = sensor_para->reg_info;
	sensor_para->reg_info =
		kmalloc(sizeof(AK_ISP_SENSOR_REG_INFO) * sensor_para->num, GFP_KERNEL);
	if (!sensor_para->reg_info) {
		pr_err("%s alloc1 fail\n", __func__);
		ret = -ENOMEM;
		goto alloc1_fail;
	} else {
		if (copy_from_user(sensor_para->reg_info, p_reg_info,
					sizeof(AK_ISP_SENSOR_REG_INFO) * sensor_para->num)) {
			pr_err("%s cp2 fail\n", __func__);
			ret = -EFAULT;
			goto cp2_fail;
		}

		if (priv->para.reg_info) {
			devm_kfree(&client->dev, priv->para.reg_info);
			priv->para.reg_info = NULL;
			priv->para.num = 0;
		}

		/*store sensor configurtion*/
		priv->para.num = sensor_para->num;
		priv->para.reg_info = devm_kzalloc(&client->dev,
				sizeof(AK_ISP_SENSOR_REG_INFO)*sensor_para->num, GFP_KERNEL);
		if (!priv->para.reg_info) {
			pr_err("%s alloc2 fail\n", __func__);
			goto alloc2_fail;
		}
		memcpy(priv->para.reg_info, sensor_para->reg_info,
				sizeof(AK_ISP_SENSOR_REG_INFO)*sensor_para->num);

		pr_debug("%s para.num;%d\n", __func__, priv->para.num);
	}

	kfree(sensor_para->reg_info);
	return 0;

alloc2_fail:
cp2_fail:
	kfree(sensor_para->reg_info);
alloc1_fail:
	return ret;
}

static long jxfxx_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jxfxx_priv *priv = to_jxfxx(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			jxfxx_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = jxfxx_get_max_exp_for_fps(sd, &ctrl);
			if (!ret)
				exp_for_fps->max_exp = ctrl.value;
			break;
		}

		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

/*
 * XXXX_get_fmt -
 * get format
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @format:			return format
 */
static int jxfxx_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct jxfxx_priv *priv = to_jxfxx(client);
	static struct jxfxx_win_size win = {
		.name = "default",
		.width = SENSOR_OUTPUT_WIDTH,
		.height = SENSOR_OUTPUT_HEIGHT,
		.regs = NULL,
	};

	if (format->pad)
		return -EINVAL;

	if (!priv->win) {
		priv->win = &win;
		priv->cfmt_code = MEDIA_BUS_FMT_UYVY8_2X8;
	}

	mf->width	= priv->win->width;
	mf->height	= priv->win->height;
	mf->code	= priv->cfmt_code;

	switch (mf->code) {
		case MEDIA_BUS_FMT_RGB565_2X8_BE:
		case MEDIA_BUS_FMT_RGB565_2X8_LE:
			mf->colorspace = V4L2_COLORSPACE_SRGB;
			break;
		default:
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_UYVY8_2X8:
			mf->colorspace = V4L2_COLORSPACE_JPEG;
	}
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

/*
 * XXXX_set_fmt -
 * set format
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @format:			format to set
 */
static int jxfxx_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;

	if (format->pad)
		return -EINVAL;

	mf->field	= V4L2_FIELD_NONE;

	switch (mf->code) {
		case MEDIA_BUS_FMT_RGB565_2X8_BE:
		case MEDIA_BUS_FMT_RGB565_2X8_LE:
			mf->colorspace = V4L2_COLORSPACE_SRGB;
			break;
		default:
			mf->code = MEDIA_BUS_FMT_UYVY8_2X8;
		case MEDIA_BUS_FMT_YUYV8_2X8:
		case MEDIA_BUS_FMT_UYVY8_2X8:
			mf->colorspace = V4L2_COLORSPACE_JPEG;
	}

	if (cfg)
		cfg->try_fmt = *mf;
	return 0;
}

/*
 * XXXX_set_fmt -
 * set format
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @sel:			return selection
 */
static int jxfxx_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	sel->r.left = SENSOR_VALID_OFFSET_X;
	sel->r.top = SENSOR_VALID_OFFSET_Y;
	sel->r.width = SENSOR_OUTPUT_WIDTH;
	sel->r.height = SENSOR_OUTPUT_HEIGHT;

	return 0;
}

/*
 * XXXX_enum_bus -
 * enum bus
 * pad functions
 *
 * @sd:				subdev
 * @cfg:			pointer to pad config
 * @code:			return code of bus type
 */
static int jxfxx_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(jxfxx_codes))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_YUYV8_2X8;

	return 0;
}

/*
 * XXXX_g_crop -
 * get crop
 * video functions
 *
 * @sd:				subdev
 * @a:				return crop
 */
static int jxfxx_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	a->c.left	= SENSOR_VALID_OFFSET_X;
	a->c.top	= SENSOR_VALID_OFFSET_Y;
	a->c.width	= SENSOR_OUTPUT_WIDTH;
	a->c.height	= SENSOR_OUTPUT_HEIGHT;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

/*
 * XXXX_cropcap -
 * get crop capbilities
 * video functions
 *
 * @sd:				subdev
 * @a:				return crop capbilities
 */
static int jxfxx_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	jxfxx_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
	a->bounds.left			= SENSOR_VALID_OFFSET_X;
	a->bounds.top			= SENSOR_VALID_OFFSET_Y;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	pr_err("%s bounds.width:%d, bounds.height:%d\n", __func__, a->bounds.width, a->bounds.height);

	return 0;
}

/*
 * XXXX_video_probe -
 * do some process in subdev probe
 * locale functions
 *
 * @sd:				subdev
 * @a:				return crop capbilities
 */
static int jxfxx_video_probe(struct i2c_client *client)
{
	struct jxfxx_priv *priv = to_jxfxx(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops jxfxx_ctrl_ops = {
	.g_volatile_ctrl	= jxfxx_g_volatile_ctrl,
	.s_ctrl				= jxfxx_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops jxfxx_subdev_core_ops = {
	.s_power	= jxfxx_core_s_power,
	.g_ctrl		= jxfxx_core_g_ctrl,
	.s_ctrl		= jxfxx_core_s_ctrl,
	.ioctl		= jxfxx_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int jxfxx_g_mbus_config(struct v4l2_subdev *sd,
		struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_MASTER |
		V4L2_MBUS_VSYNC_ACTIVE_HIGH | V4L2_MBUS_HSYNC_ACTIVE_HIGH |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = 0;

	return 0;
}

/*
 * XXXX_s_crop -
 * set crop
 * video functions
 *
 * @sd:				subdev
 * @crop:			crop to set
 */
static int jxfxx_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	printk(KERN_ERR "%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops jxfxx_subdev_video_ops = {
	.s_stream	= jxfxx_s_stream,
	.cropcap	= jxfxx_cropcap,
	.g_crop		= jxfxx_g_crop,
	.g_mbus_config	= jxfxx_g_mbus_config,
	.s_crop		= jxfxx_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops jxfxx_subdev_pad_ops = {
	.enum_mbus_code = jxfxx_enum_mbus_code,
	.get_fmt	= jxfxx_get_fmt,
	.set_fmt	= jxfxx_set_fmt,
	.get_selection	= jxfxx_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops jxfxx_subdev_ops = {
	.core	= &jxfxx_subdev_core_ops,
	.video	= &jxfxx_subdev_video_ops,
	.pad	= &jxfxx_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct jxfxx_priv *priv)
{
	struct device_node *np = client->dev.of_node;

	/*it is exist reset but lack of pwdn gpio in most case*/
	priv->gpio_reset = of_get_named_gpio(np, "reset-gpio", 0);
	priv->gpio_pwdn = of_get_named_gpio(np, "pwdn-gpio", 0);

	if (priv->gpio_reset >= 0) {
		devm_gpio_request(&client->dev, priv->gpio_reset, "sensor-reset");
	}

	if (priv->gpio_pwdn >= 0) {
		devm_gpio_request(&client->dev, priv->gpio_pwdn, "sensor-pwdn");
	}

	return 0;
}

/*
 * XXXX_match - check sensor id for match driver
 * @priv:			pointer to pirvate structure
 *
 * @RETURN:	0-match fail; 1-match success
 */
static int jxfxx_match(struct jxfxx_priv *priv)
{
	int pid;

	if (!check_id)
		return 1;

	priv->cb_info.cb->sensor_set_power_on_func(priv);
	pid = priv->cb_info.cb->sensor_probe_id_func(priv);
	if (pid <= 0) {
		pr_err("%s fail\n", __func__);
		return 0;
	}

	return 1;
}

/*
 * XXXX_probe -
 * driver probe after platform probe ok
 * i2c_driver functions
 *
 * @client:			pointor to i2c client
 * @did:			driver ids
 */
static int jxfxx_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct jxfxx_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
				"jxfxx: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct jxfxx_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
				"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}
	g_jxfxx_priv = priv;

	/*parse of node*/
	sensor_of_parse(client, priv);

	/*
	 * the current sensor slave address to client.
	 * the address from dts may incorrect
	 * */
	client->addr = SENSOR_I2C_ADDR;
	priv->cb_info.cb = &jxfxx_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!jxfxx_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &jxfxx_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &jxfxx_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &jxfxx_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);

	/*private ctrl*/
	priv->ctrl_sensor_get_id = v4l2_ctrl_new_custom(&priv->hdl, &config_sensor_get_id, NULL);
	if (!priv->ctrl_sensor_get_id)
		pr_err("creat ctrl_sensor_get_id fail\n");

	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		dev_err(&client->dev, "Hdl error\n");
		ret = priv->hdl.error;
		goto err_clk;
	}

	ret = jxfxx_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "OV2640 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "jxfxx Probed success, subdev:%p\n", &priv->subdev);

	/*ak platform need export some informations from sensor*/
	call_sensor_sys_init();

	return 0;

err_videoprobe:
	v4l2_ctrl_handler_free(&priv->hdl);
err_clk:
	return ret;
}

/*
 * XXXX_remove -
 * driver probe after platform remove ok
 * i2c_driver functions
 *
 * @client:			pointor to i2c client
 */
static int jxfxx_remove(struct i2c_client *client)
{
	struct jxfxx_priv       *priv = to_jxfxx(client);

	pr_err("%s %d\n", __func__, __LINE__);

	if (!priv) {
		pr_err("%s had remove\n", __func__);
		return 0;
	}

	/*unregister async subdev*/
	v4l2_async_unregister_subdev(&priv->subdev);
	/*unregister subdev*/
	v4l2_device_unregister_subdev(&priv->subdev);
	/*unregister handler*/
	v4l2_ctrl_handler_free(&priv->hdl);
	/*unregister sys node*/
	call_sensor_sys_deinit();
	return 0;
}

static const struct i2c_device_id jxfxx_id[] = {
	{ "jxfxx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jxfxx_id);

static const struct of_device_id jxfxx_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, jxfxx_of_match);

static struct i2c_driver jxfxx_i2c_driver = {
	.driver = {
		.name = "jxfxx",
		.of_match_table = of_match_ptr(jxfxx_of_match),
	},
	.probe    = jxfxx_probe,
	.remove   = jxfxx_remove,
	.id_table = jxfxx_id,
};

module_i2c_driver(jxfxx_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for jxfxx sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.7");

/*sensor i2c write*/
static int jxfxx_write(const struct i2c_client *client, int reg, int value)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = value,
	};

	return i2c_write(&trans);
}

/*sensor i2c read*/
static int jxfxx_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	return i2c_read(&trans);
}

/*
 * The sensor must set callbacks
 *
 * */
static int jxfxx_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	int i;
	AK_ISP_SENSOR_REG_INFO *preg_info;
	struct jxfxx_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;

	jxfxx_sensor_probe_id_func(arg);  //use IIC bus

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;
	for (i = 0; i < para->num; i++) {
#if 0
		{
			int value;

			value = jxfxx_read(client, preg_info->reg_addr);
			pr_err("reg:%x read value:%x\n", preg_info->reg_addr, value);
		}
#endif
		if (preg_info->reg_addr == DELAY_FLAG)
			mdelay(preg_info->value);
		else
			jxfxx_write(client, preg_info->reg_addr, preg_info->value);
#if 0
		{
			int value;

			value = jxfxx_read(client, preg_info->reg_addr);
			pr_err("reg:%x write value:%x, read back value:%x\n",
					preg_info->reg_addr, preg_info->value, value);
		}
#endif

		preg_info++;
	}

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;
	priv->fps_info.to_fps_value = 0;

	return 0;
}

/*read sensor register*/
static int jxfxx_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct jxfxx_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return jxfxx_read(client, reg_addr);
}
/*write sensor register*/
static int jxfxx_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct jxfxx_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return jxfxx_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int jxfxx_sensor_read_id_func(void *arg)   //no use IIC bus
{
	if (!arg)
		arg = g_jxfxx_priv;

	if (!SENSOR_ID)
		jxfxx_sensor_probe_id_func(arg);  //use IIC bus

	return SENSOR_ID;
}

static unsigned int f22_cmos_gains_convert(unsigned int a_gain, unsigned int d_gain,
		unsigned int *a_gain_out ,unsigned int *d_gain_out)
{
	/*
	 * analog gain from F37 datasheet:
	 * Programmable gain, valid 00 to 3F,
	 * Total gain = 2^PGA[6:4]*(1+PGA[3:0]/16)
	 *
	 * ==>total=2^m * (1 + n/16)
	 * ==>total*16=2^m * (16 + n)
	 * m>=0
	 * n>=0
	 * define N=16+n
	 */

	/*
	 * a bug for f37:
	 * when again=64times that is REG0x00=0x60, it's luminance low than REG0x00=0x5f.
	 * so a_gain be set to low than 64times.
	 */

	int m;
	int tmp;
	int N; /*N=16+n*/
	int n;

	if (SENSOR_ID_F37 == SENSOR_ID) {
		/*F37 limit: lower than 64times again*/
		if (a_gain >= 64 * 16)
			a_gain = 64 * 16 - 1;
	}

	m = 0;
	tmp = a_gain;
	N = tmp; /*N=16+n*/

	while (tmp >= 16) {
		N = tmp;
		tmp >>= 1;
		m++;
	}

	if (m >= 1)
		/*should run here*/
		m--;

	m <<= 4;
	if (N >= 16)
		/*should run here*/
		n = N - 16;
	else
		n = N;

	tmp = m | n;
	*a_gain_out = tmp;

	pr_debug("tmp:%d,m:0x%x,n:0x%x\n",tmp, m, n);

	return tmp;
}

/**
 * 根据 SENSOR 4A/4C/4F 三个值获取 BLC Gb 的值。
 */
static int get_blc_gb (int reg0x4a, int reg0x4c, int reg0x4f)
{
	int blc_gb = 0;

	reg0x4a >>= 6;
	reg0x4a &= 0x01; ///< 0X4A BIT6
	reg0x4f >>= 2;
	reg0x4f &= 0x03; ///< 0X4F BIT[3:2]
	reg0x4c &= 0xff;

	blc_gb = (reg0x4a << 10) | (reg0x4f << 8) | reg0x4c;
	return blc_gb;

}

/*set sensor again*/
static int jxfxx_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	struct jxfxx_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned int tmp_a_gain;
	unsigned int tmp_d_gain;
	unsigned int tmp_a_gain_out;
	unsigned int tmp_d_gain_out;

	pr_debug("jxfxx again:%d\n", a_gain);

	tmp_a_gain = a_gain>>4;
	tmp_d_gain = 0;

	f22_cmos_gains_convert(tmp_a_gain, tmp_d_gain,  &tmp_a_gain_out ,&tmp_d_gain_out);

	if (SENSOR_ID_F23 == SENSOR_ID) {

		// 背光补偿调整策略。
		int const blc_gb = get_blc_gb (
				jxfxx_read(client, 0x4a),
				jxfxx_read(client, 0x4c),
				jxfxx_read(client, 0x4f));
		int const blc_gb_thrd = get_blc_gb(0x05, 0x93, 0);
		unsigned int reg0x1f;

		if (blc_gb > blc_gb_thrd) {
			jxfxx_write(client, 0x8e, 0x04);
		} else {
			jxfxx_write(client, 0x8e, 0x00);
		}

		// F23 太阳黑子策略。
		reg0x1f = jxfxx_read(client,0x1f);
		if(a_gain < 1024)
		{
			jxfxx_write(client,0xc0,0x66);
			jxfxx_write(client,0xc1,0x04);
			jxfxx_write(client,0xc2,0x0c);
			jxfxx_write(client,0xc3,0x00);
			jxfxx_write(client,0x1f,0x80|reg0x1f);
		}else{
			jxfxx_write(client,0xc0,0x0c);
			jxfxx_write(client,0xc1,0x40);
			jxfxx_write(client,0xc2,0x66);
			jxfxx_write(client,0xc3,0x44);
			jxfxx_write(client,0x1f,0x80|reg0x1f);
		}

	} else if (SENSOR_ID_F37 == SENSOR_ID) {
		// F37 太阳黑子策略。

		unsigned int reg0x1f = jxfxx_read(client,0x1f);
		if (tmp_a_gain_out < 0x10) {
			jxfxx_write(client,0xc0,0x2f);
			jxfxx_write(client,0xc1,0x64);
			jxfxx_write(client,0xc2,0x0c);
			jxfxx_write(client,0xc3,0x40);
			jxfxx_write(client,0xc4,0x82);
			jxfxx_write(client,0xc5,0x23);
			jxfxx_write(client,0x1f,0x80|reg0x1f);
		} else {
			jxfxx_write(client,0xc0,0x2f);
			jxfxx_write(client,0xc1,0x44);
			jxfxx_write(client,0xc2,0x0c);
			jxfxx_write(client,0xc3,0x00);
			jxfxx_write(client,0xc4,0x82);
			jxfxx_write(client,0xc5,0x21);
			jxfxx_write(client,0x1f,0x80|reg0x1f);
		}
	}

	jxfxx_write(client, 0x00, tmp_a_gain_out);
	return A_GAIN_EFFECT_FRAMES;
}
/*set sensor dgain*/
static int jxfxx_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	return D_GAIN_EFFECT_FRAMES;
}
/*set sensor exp time*/
static int jxfxx_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	struct jxfxx_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned char exposure_time_msb;
	unsigned char exposure_time_lsb;

	pr_debug("jxfxx exp:%d\n", exp_time);

	exposure_time_msb =(exp_time>>8)&0xff;
	exposure_time_lsb = exp_time&0xff;

	jxfxx_write(client, 0x02,exposure_time_msb);
	jxfxx_write(client, 0x01,exposure_time_lsb);
	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int jxfxx_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int jxfxx_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int jxfxx_sensor_set_standby_out_func(void *arg)
{
	return 0;
}
/*low level read sensor ID, user i2c ops*/
static int jxfxx_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct jxfxx_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int id;
	int value;

	value = jxfxx_read(client, 0x0a);
	id = value << 8;
	value = jxfxx_read(client, 0x0b);
	id |= value;

	pr_err("%s id:%x\n", __func__, id);

	switch (id) {
		case SENSOR_ID_F23:
		case SENSOR_ID_F35:
			MIPI_LANES	= 2;
			MAX_FPS		= 30;
		case SENSOR_ID_F22:
		case SENSOR_ID_F28:
		case SENSOR_ID_F37:
			SENSOR_ID = id;
			break;
		case SENSOR_ID_F37P:
			MIPI_LANES	= 2;
			MAX_FPS		= 25;
			SENSOR_ID 	= id;
			break;
		default:
			SENSOR_ID = 0;
			break;
	}

	return SENSOR_ID;
}
/*get resolution*/
static int jxfxx_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int jxfxx_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int jxfxx_sensor_get_fps_func(void *arg)
{
	struct jxfxx_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int jxfxx_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type jxfxx_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
/*get self definded params*/
static int jxfxx_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct jxfxx_priv *priv = arg;

	switch (param) {
		case GET_INTERFACE:
			*((int *)value) = SENSOR_IO_INTERFACE;
			break;

		case GET_IO_LEVEL:
			*((int *)value) = SENSOR_IO_LEVEL;
			break;

		case GET_MIPI_MHZ:
			*((int *)value) = MIPI_MBPS;
			break;

		case GET_MIPI_LANES:
			*((int *)value) = MIPI_LANES;
			break;

		case GET_RESET_GPIO:
			*((int *)value) = priv->gpio_reset;
			break;

		case GET_PWDN_GPIO:
			*((int *)value) = priv->gpio_pwdn;
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int jxfxx_sensor_set_power_on_func(void *arg)
{
	struct jxfxx_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_output(priv->gpio_pwdn, !SENSOR_PWDN_LEVEL);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_output(priv->gpio_reset, !SENSOR_RESET_LEVEL);
		msleep(50);
		gpio_direction_output(priv->gpio_reset, SENSOR_RESET_LEVEL);
		msleep(50);
		gpio_direction_output(priv->gpio_reset, !SENSOR_RESET_LEVEL);
		msleep(100);
	}

	return 0;
}
/*sensor power off*/
static int jxfxx_sensor_set_power_off_func(void *arg)
{
	struct jxfxx_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}
/*set sensor fps*/
static int jxfxx_fps_to_vts(struct jxfxx_priv *priv, const int fps)
{
	int vts;

	if (SENSOR_IO_INTERFACE == DVP_INTERFACE) {
		/* actual mclk=24MHz pclk=76.8MHz, but should 30fps * (1280*2) * 1125 = 76.8MHz
		 * so all follow set fps is too higher than "FPS"
		 */
		switch (fps) {
			case 25:
				vts = 0x04b0;
				break;

			case 20:
				vts = 0x05dc;
				break;

			case 15:
				vts = 0x07d0;
				break;

			case 14://14.28
				vts = 0x0834;//2100.8403361344537815126050420168
				break;

			case 12:
				vts = 0x0960;
				break;

			case 10:
				vts = 0x0bb8;
				break;

			case 8:
				vts = 0x0ea6;
				break;

			case 5:
				vts = 0x1770;
				break;

			default:
				vts = 0;
				pr_err("%s set fps fail\n", __func__);
				break;
		}
	} else if (SENSOR_IO_INTERFACE == MIPI_INTERFACE) {
		/*
		 * F37_Map#74_No_3445_20190829.ini
		 * [SensorType]
		 * 0=F37AF_074_20190827
		 *
		 * [F37AF_074_20190827]
		 * InitREGdef=INI_Register
		 * IniINFOdef=INI_Category
		 * IniVersion=20180905
		 * Project=104	;F37
		 * Width=1920
		 * Height=1080
		 * disWidth=1920
		 * disHeight=1080
		 * FrameWidth=2560
		 * FrameHeight=1125
		 * H_pad=0
		 * V_pad=0
		 * SrOutputFormats=1
		 * Interface=1
		 * HDR_Mode=0
		 * MclkRate=24.000
		 * DVPClkRate=72.00000
		 * SensorLineMode=0
		 * MipiImgVc=0
		 * MipiImgVc1=1
		 * LVDSformat=0
		 * PixelRate=72.00000		;SysClk = 72.00000
		 * MipiClkRate=360.00000
		 *
		 * [INI_Category]
		 * FPS=25.00000
		 * DefaultReg_Ver=AF
		 * Start=F37_Start_2019031300.soi
		 * PLL=F37_PLL_2019052900.soi
		 * DAC=F37_DAC_2019031301.soi
		 * Timing=F37_Timing_2019080601.soi
		 * Interface=F37_Interface_2019053100.soi
		 * Sampling=F37_Sampling_2019080600.soi
		 * Power=F37_Power_2019070900.soi
		 * AE=F37_AE_2019080601.soi
		 * DisMode=F37_DisMode_2019041801.soi
		 * End=F37_End_2019031300.soi
		 * Factory=
		 * FT=
		 *
		 * ;PC:1842272883
		 */
#if 1
		switch (fps) {
			case 12:
				vts = 1250;
				break;

			case 14:
				vts = 1428;
				break;

			default:
				vts = fps * 100;
				break;
		}

		/*vts = 7200000000UL / 2560 / vts / 2;*/
		vts = 3600000000UL / 1280 / vts;
#else
		switch (fps) {
			case 25:
				tmp = 0x0465;
				break;

			case 20:
				tmp = 0x056e;
				break;

			case 15:
				tmp = 0x073d;
				break;

			case 14:
				tmp = 0x08b0;
				break;

			case 12:
				tmp = 0x08b0;
				break;

			case 10:
				tmp = 0x0adc;
				break;

			case 8:
				tmp = 0x0d93;
				break;

			case 5:
				tmp = 0x15b9;
				break;

			default:
				pr_err("%s set fps fail\n", __func__);
				vts = 0;
				break;
		}
#endif
	}

	return vts;
}

/*set sensor fps*/
static int jxfxx_sensor_set_fps_func(void *arg, const int fps)
{
	struct jxfxx_priv *priv = arg;
	int vts;

	vts = jxfxx_fps_to_vts(priv, fps);

	if (vts > 0) {
		priv->fps_info.to_fps_value = vts;
		priv->fps_info.to_fps = fps;
	}
	return 0;
}

static void jxfxx_set_fps_async(struct jxfxx_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (priv->fps_info.to_fps != priv->fps_info.current_fps) {
		int value = priv->fps_info.to_fps_value;

		jxfxx_write(client, 0x23, (value >> 8) & 0xff);
		jxfxx_write(client, 0x22, value & 0xff);
		priv->fps_info.current_fps = priv->fps_info.to_fps;
	}
}

static AK_ISP_SENSOR_CB jxfxx_cb = {
	.sensor_init_func = jxfxx_sensor_init_func,
	.sensor_read_reg_func = jxfxx_sensor_read_reg_func,
	.sensor_write_reg_func = jxfxx_sensor_write_reg_func,
	.sensor_read_id_func = jxfxx_sensor_read_id_func,
	.sensor_update_a_gain_func = jxfxx_sensor_update_a_gain_func,
	.sensor_update_d_gain_func = jxfxx_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func = jxfxx_sensor_updata_exp_time_func,
	.sensor_timer_func = jxfxx_sensor_timer_func,
	.sensor_set_standby_in_func = jxfxx_sensor_set_standby_in_func,
	.sensor_set_standby_out_func = jxfxx_sensor_set_standby_out_func,
	.sensor_probe_id_func = jxfxx_sensor_probe_id_func,
	.sensor_get_resolution_func = jxfxx_sensor_get_resolution_func,
	.sensor_get_mclk_func = jxfxx_sensor_get_mclk_func,
	.sensor_get_fps_func = jxfxx_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func = jxfxx_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func = jxfxx_sensor_get_bus_type_func,
	.sensor_get_parameter_func = jxfxx_sensor_get_parameter_func,
	.sensor_set_power_on_func = jxfxx_sensor_set_power_on_func,
	.sensor_set_power_off_func = jxfxx_sensor_set_power_off_func,
	.sensor_set_fps_func = jxfxx_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return jxfxx_sensor_read_id_func(NULL);
}

static char *sensor_if_func(void)
{
	static char ifstr[16] = "dvp";

	if (SENSOR_IO_INTERFACE == MIPI_INTERFACE)
		sprintf(ifstr, "mipi%d", MIPI_LANES);

	return ifstr;
}

static int call_sensor_sys_init(void)
{
	return sensor_sys_init(sensor_id_func, sensor_if_func);
}

static int call_sensor_sys_deinit(void)
{
	sensor_sys_exit();
	return 0;
}

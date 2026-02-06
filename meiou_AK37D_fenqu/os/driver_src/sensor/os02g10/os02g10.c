/*
 * os02g10 Camera Driver
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
#define SENSOR_PWDN_LEVEL		1
#define SENSOR_RESET_LEVEL		0
#define SENSOR_I2C_ADDR			0x3c
#define SENSOR_ID				0x5602
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
#define MIPI_MBPS				720
#define MIPI_LANES				2
#define MAX_FPS					30
#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	1	/*adjust a_gain for 1/2 frames*/
#define D_GAIN_EFFECT_FRAMES	1	/*adjust d_gain for 1/2 frames*/

#define DELAY_FLAG		(0xffff)

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct os02g10_win_size {
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
struct os02g10_aec_parms {
	int pclk;
	int cur_hts;
	int cur_vts;
	int cur_exp;
};

static const struct v4l2_ctrl_ops os02g10_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &os02g10_ctrl_ops,
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
struct os02g10_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct os02g10_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct os02g10_aec_parms aec_parms;

	AK_ISP_SENSOR_INIT_PARA para;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};

static AK_ISP_SENSOR_CB os02g10_cb;

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void os02g10_set_fps_async(struct os02g10_priv *priv);
static int __os02g10_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int os02g10_sensor_read_id_func(void *arg);   //no use IIC bus
static int os02g10_sensor_get_resolution_func(void *arg, int *width, int *height);
static int os02g10_sensor_set_fps_func(void *arg, const int fps);
static int os02g10_fps_to_vts(struct os02g10_priv *priv, const int fps);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 os02g10_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct os02g10_priv *to_os02g10(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct os02g10_priv,
			    subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct os02g10_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int os02g10_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int os02g10_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
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
static int os02g10_s_ctrl(struct v4l2_ctrl *ctrl)
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
static int os02g10_core_s_power(struct v4l2_subdev *sd, int on)
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
static int os02g10_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = os02g10_sensor_read_id_func(NULL);   //no use IIC bus
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
static int os02g10_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct os02g10_priv *priv = to_os02g10(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int os02g10_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	/*
	 * from datasheet:
	 * the OS02G10 exposure registers are P1:0x03 and P1:0x04. The unit is Tline.
	 * Mininum exposure is 4 Tline. The maxinum exposure is VTS-8. If the
	 * exposure is longer than VTS-8, the OS02G10 will automatically prolong
	 * the current VTS.
	 */
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct os02g10_priv *priv = to_os02g10(client);
	int fps = ctrl->value;
	int vts = os02g10_fps_to_vts(priv, fps);

	ctrl->value = vts - 8;
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
static int os02g10_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = os02g10_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = os02g10_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = os02g10_get_max_exp_for_fps(sd, ctrl);
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
static int os02g10_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct os02g10_priv *priv = to_os02g10(client);
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
static int os02g10_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct os02g10_priv *priv = to_os02g10(client);
	int fps = ctrl->value;

	os02g10_sensor_set_fps_func(priv, fps);
	os02g10_set_fps_async(priv);
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
static int os02g10_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = os02g10_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = os02g10_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int os02g10_store_initial_regs(struct os02g10_priv *priv, void *arg)
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

static long os02g10_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct os02g10_priv *priv = to_os02g10(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			os02g10_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = os02g10_get_max_exp_for_fps(sd, &ctrl);
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
static int os02g10_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct os02g10_priv *priv = to_os02g10(client);
	static struct os02g10_win_size win = {
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
static int os02g10_set_fmt(struct v4l2_subdev *sd,
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
static int os02g10_get_selection(struct v4l2_subdev *sd,
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
static int os02g10_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(os02g10_codes))
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
static int os02g10_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
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
static int os02g10_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	os02g10_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
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
static int os02g10_video_probe(struct i2c_client *client)
{
	struct os02g10_priv *priv = to_os02g10(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops os02g10_ctrl_ops = {
	.g_volatile_ctrl	= os02g10_g_volatile_ctrl,
	.s_ctrl				= os02g10_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops os02g10_subdev_core_ops = {
	.s_power	= os02g10_core_s_power,
	.g_ctrl		= os02g10_core_g_ctrl,
	.s_ctrl		= os02g10_core_s_ctrl,
	.ioctl		= os02g10_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int os02g10_g_mbus_config(struct v4l2_subdev *sd,
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
static int os02g10_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	pr_info("%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops os02g10_subdev_video_ops = {
	.s_stream	= os02g10_s_stream,
	.cropcap	= os02g10_cropcap,
	.g_crop		= os02g10_g_crop,
	.g_mbus_config	= os02g10_g_mbus_config,
	.s_crop		= os02g10_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops os02g10_subdev_pad_ops = {
	.enum_mbus_code = os02g10_enum_mbus_code,
	.get_fmt	= os02g10_get_fmt,
	.set_fmt	= os02g10_set_fmt,
	.get_selection	= os02g10_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops os02g10_subdev_ops = {
	.core	= &os02g10_subdev_core_ops,
	.video	= &os02g10_subdev_video_ops,
	.pad	= &os02g10_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct os02g10_priv *priv)
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
static int os02g10_match(struct os02g10_priv *priv)
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
static int os02g10_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct os02g10_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"os02g10: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct os02g10_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	/*parse of node*/
	sensor_of_parse(client, priv);

	/*
	 * the current sensor slave address to client.
	 * the address from dts may incorrect
	 * */
	client->addr = SENSOR_I2C_ADDR;
	priv->cb_info.cb = &os02g10_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!os02g10_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &os02g10_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &os02g10_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &os02g10_ctrl_ops,
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

	ret = os02g10_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "OV2640 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "os02g10 Probed success, subdev:%p\n", &priv->subdev);

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
static int os02g10_remove(struct i2c_client *client)
{
	struct os02g10_priv       *priv = to_os02g10(client);

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

static const struct i2c_device_id os02g10_id[] = {
	{ "os02g10", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, os02g10_id);

static const struct of_device_id os02g10_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, os02g10_of_match);

static struct i2c_driver os02g10_i2c_driver = {
	.driver = {
		.name = "os02g10",
		.of_match_table = of_match_ptr(os02g10_of_match),
	},
	.probe    = os02g10_probe,
	.remove   = os02g10_remove,
	.id_table = os02g10_id,
};

module_i2c_driver(os02g10_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for os02g10 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.0");

/*sensor i2c write*/
static int os02g10_write(const struct i2c_client *client, int reg, int value)
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
static int os02g10_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	//pr_info("%s client:%p, addr:%x\n", __func__, client, client->addr);
	return i2c_read(&trans);
}

/*
 * The sensor must set callbacks
 *
 * */
static int os02g10_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;
	AK_ISP_SENSOR_REG_INFO *preg_info;
    int hts = 0;
    int vts = 0;
	int i;

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;
    //sensor 寄存器初始化
	for (i = 0; i < para->num; i++) {

		os02g10_write(client, preg_info->reg_addr, preg_info->value);

		preg_info++;
	}

    os02g10_write(client, 0xfd, 0x01);
    os02g10_write(client, 0x01, 0x01);
    hts = (os02g10_read(client, 0x41) << 8) | (os02g10_read(client, 0x42));
    vts = (os02g10_read(client, 0x4E) << 8) | (os02g10_read(client, 0x4F));
    priv->aec_parms.pclk = hts * vts * MAX_FPS;//计算PCLK

    priv->aec_parms.cur_exp =
		(os02g10_read(client, 0x03) << 8) | (os02g10_read(client, 0x04));
    //pr_err("####### PCLK = %d ######", PCLK);

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;
	priv->fps_info.to_fps_value = vts;
    priv->aec_parms.cur_hts = hts;
    priv->aec_parms.cur_vts = vts;

	return 0;
}

/*read sensor register*/
static int os02g10_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return os02g10_read(client, reg_addr);
}
/*write sensor register*/
static int os02g10_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return os02g10_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int os02g10_sensor_read_id_func(void *arg)   //no use IIC bus
{
	return SENSOR_ID;
}

static int aec_parms_init(struct os02g10_priv *priv)
{
	priv->aec_parms.pclk	= 0;
	priv->aec_parms.cur_hts	= 0;
	priv->aec_parms.cur_vts	= 0;
	priv->aec_parms.cur_exp	= 0;

	return 0;
}

/*set sensor again*/
static int os02g10_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	/*
	 * max again < 16 times = 4096
	 * */
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;

    unsigned int tmp_gain = a_gain;
    int gain_level = 0;
    int gain_off = 0;
    int target_gain = 0;

    //pr_err("############### Set a_gain  = %d ###############\r\n", a_gain);
    if (tmp_gain < 512) {//1--2
        gain_level = 1;
        gain_off = (tmp_gain - 256) / 16;
        target_gain = 0x10 + gain_off;
    } else if(tmp_gain < 1024) {//2--4
        gain_level = 2;
        gain_off = ((tmp_gain - 512) / 32) * gain_level;
        target_gain = 0x20 + gain_off;
    } else if(tmp_gain < 2048) {//4--8
        gain_level = 4;
        gain_off = ((tmp_gain - 1024) / 64) * gain_level;
        target_gain = 0x40 + gain_off;
    } else if(tmp_gain < 4096) {//8--15.5
        gain_level = 8;
        gain_off = ((tmp_gain - 2048) / 128) * gain_level;
        target_gain = 0x80 + gain_off;
    }

    //pr_err("############### Set a_gain reg = 0x%x ###############\r\n", target_gain);
    os02g10_write(client, 0xFD, 0x01);
	os02g10_write(client, 0x24, target_gain);
    os02g10_write(client, 0x01, 0x01);

	return EXP_EFFECT_FRAMES;
}
/*set sensor dgain*/
static int os02g10_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
#if 0
	/*
	 * max dgain < 32 times = 8192
	 * */
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned int corse_gain, fine_gain;

	if(d_gain<512) {
		/*
		 * [1,2)
		 * 1/32
		 * */
		corse_gain = 0x0;
		fine_gain = 0x80 + (d_gain - 256) / 2; /*256/64=4*/
		fine_gain &= 0xfc;
	} else if(d_gain<1024) {
		/*
		 * [2,4)
		 * 1/16
		 * */
		corse_gain = 0x1;
		fine_gain = 0x80 + (d_gain - 512) / 4;/*512/64=8*/
		fine_gain &= 0xfc;
	} else if(d_gain<2048) {
		/*
		 * [4,8)
		 * 1/8
		 * */
		corse_gain = 0x3;
		fine_gain = 0x80 + (d_gain - 1024) / 8;/*1024/64=16*/
		fine_gain &= 0xfc;
	} else if(d_gain<4096) {
		/*
		 * [8,16)
		 * 1/4
		 * */
		corse_gain = 0x7;
		fine_gain = 0x80 + (d_gain - 2048) / 16;/*2048/64=32*/
		fine_gain &= 0xfc;
	} else {
		corse_gain = 0x7;
		fine_gain = 0xfc;
		fine_gain &= 0xfc;
	}

	os02g10_write(client, 0x3e06, corse_gain);
	os02g10_write(client, 0x3e07, fine_gain);
#endif
	return D_GAIN_EFFECT_FRAMES;
}

static void os02g10_exp_async(struct i2c_client *client, int to_exp)
{
    //pr_err("###### to_exp:%d ######\n",to_exp);

    os02g10_write(client, 0xfd, 0x01);
    os02g10_write(client, 0x03, (to_exp >> 8) & 0xff);
    os02g10_write(client, 0x04, to_exp & 0xff);
    os02g10_write(client, 0x01, 0x01);
}

/*set sensor exp time*/
static int os02g10_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	/*
	 * from datasheet:
	 * the OS02G10 exposure registers are P1:0x03 and P1:0x04. The unit is Tline.
	 * Mininum exposure is 4 Tline. The maxinum exposure is VTS-8. If the
	 * exposure is longer than VTS-8, the OS02G10 will automatically prolong
	 * the current VTS.
	 */
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int to_exp;
	int to_vts = priv->fps_info.to_fps_value;

	//pr_err("###### exp_time:%d ######\n",exp_time);

	to_exp = (exp_time < to_vts-8) ? exp_time : (to_vts-8);
	to_exp = (to_exp >= 4) ? to_exp : 4;
	if (to_vts >= priv->aec_parms.cur_vts) {
		//帧长变长，先设帧长
		os02g10_set_fps_async(priv);
		os02g10_exp_async(client, to_exp);
	} else {
		//否则先设曝光
		os02g10_exp_async(client, to_exp);
		os02g10_set_fps_async(priv);
	}

	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int os02g10_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int os02g10_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int os02g10_sensor_set_standby_out_func(void *arg)
{
	return 0;
}
/*low level read sensor ID, user i2c ops*/
static int os02g10_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct os02g10_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int id;
	int value;

	os02g10_write(client, 0xfd, 0x00);//切换page页

	value = os02g10_read(client, 0x02);
	id = value << 8;

	value = os02g10_read(client, 0x03);
	id |= value;

	pr_info("id:%x\n", id);

	if (id == SENSOR_ID)
		return SENSOR_ID;

	return 0;
}
/*get resolution*/
static int os02g10_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int os02g10_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int os02g10_sensor_get_fps_func(void *arg)
{
	struct os02g10_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int os02g10_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type os02g10_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
/*get self definded params*/
static int os02g10_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct os02g10_priv *priv = arg;

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
static int os02g10_sensor_set_power_on_func(void *arg)
{
	struct os02g10_priv *priv = arg;

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

	aec_parms_init(priv);

	return 0;
}
/*sensor power off*/
static int os02g10_sensor_set_power_off_func(void *arg)
{
	struct os02g10_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}

static int os02g10_fps_to_vts(struct os02g10_priv *priv, const int fps)
{
	struct i2c_client *client = priv->client;
	int hts = priv->aec_parms.cur_hts;
	int vts;

    vts = priv->aec_parms.pclk;
    vts /= hts;
    vts /= fps;

	return vts;
}

/*set sensor fps*/
static int os02g10_sensor_set_fps_func(void *arg, const int fps)
{
	struct os02g10_priv *priv = arg;
	int tmp;

	tmp = os02g10_fps_to_vts(priv, fps);

	priv->fps_info.to_fps_value = tmp;
	priv->fps_info.to_fps = fps;
    //pr_err("### hts = %d ###\n", hts);
    //pr_err("### PCLK = %d ###\n", PCLK);
    //pr_err("###### set fps %d ######\n", fps);

	return 0;
}

static void os02g10_set_fps_async(struct os02g10_priv *priv)
{
	struct i2c_client *client = priv->client;
	int value = priv->fps_info.to_fps_value;

	//pr_err("####### to set fps:%d #######\n", value);
	os02g10_write(client, 0xfd, 0x01);
	os02g10_write(client, 0x4E, (value >> 8) & 0xff);
	os02g10_write(client, 0x4F, value & 0xff);
	os02g10_write(client, 0x01, 0x01);
	priv->fps_info.current_fps = priv->fps_info.to_fps;
	priv->aec_parms.cur_vts = priv->fps_info.to_fps_value;
}

static AK_ISP_SENSOR_CB os02g10_cb = {
    .sensor_init_func                   =   os02g10_sensor_init_func,
    .sensor_read_reg_func               =   os02g10_sensor_read_reg_func,
    .sensor_write_reg_func              =   os02g10_sensor_write_reg_func,
    .sensor_read_id_func                =   os02g10_sensor_read_id_func,
    .sensor_update_a_gain_func          =   os02g10_sensor_update_a_gain_func,
    .sensor_update_d_gain_func          =   os02g10_sensor_update_d_gain_func,
    .sensor_updata_exp_time_func        =   os02g10_sensor_updata_exp_time_func,
    .sensor_timer_func                  =   os02g10_sensor_timer_func,
    .sensor_set_standby_in_func         =   os02g10_sensor_set_standby_in_func,
    .sensor_set_standby_out_func        =   os02g10_sensor_set_standby_out_func,
    .sensor_probe_id_func               =   os02g10_sensor_probe_id_func,
    .sensor_get_resolution_func         =   os02g10_sensor_get_resolution_func,
    .sensor_get_mclk_func               =   os02g10_sensor_get_mclk_func,
    .sensor_get_fps_func                =   os02g10_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func   =   os02g10_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func           =   os02g10_sensor_get_bus_type_func,
	.sensor_get_parameter_func          =   os02g10_sensor_get_parameter_func,
	.sensor_set_power_on_func           =   os02g10_sensor_set_power_on_func,
	.sensor_set_power_off_func          =   os02g10_sensor_set_power_off_func,
	.sensor_set_fps_func                =   os02g10_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return os02g10_sensor_read_id_func(NULL);
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

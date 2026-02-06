/*
 * ar0230 Camera Driver
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
#define SENSOR_I2C_ADDR			0x18 
#define SENSOR_ID				0x0230
#define SENSOR_MCLK				24
#define SENSOR_REGADDR_BYTE		2
#define SENSOR_DATA_BYTE		2
#define SENSOR_OUTPUT_WIDTH		1920
#define SENSOR_OUTPUT_HEIGHT	1080
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_RAW
#define SENSOR_IO_INTERFACE		DVP_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_1V8
#define MAX_FPS					25

#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	0	/*adjust a_gain for every frames*/
#define D_GAIN_EFFECT_FRAMES	0	/*adjust d_gain for every frames*/

#define DELAY_FLAG		(0xffff)

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct ar0230_win_size {
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
struct ar0230_aec_parms {

};

static const struct v4l2_ctrl_ops ar0230_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &ar0230_ctrl_ops,
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
struct ar0230_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct ar0230_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct ar0230_aec_parms aec_parms;

	AK_ISP_SENSOR_INIT_PARA para;

	struct v4l2_ctrl *ctrl_sensor_get_id;
};

/*
 * struct fps_to_timing_info -
 * timing for some fps configuration
 *
 * @fps:			fps
 * @isp_timing:		timing configuration
 */
struct fps_to_timing_info {
	int fps;
	struct isp_timing_info isp_timing;
};

/*all support timing*/
static struct fps_to_timing_info fps_to_timing[] = {
	/*fps,oneline,	fsden,	hblank,	fsdnum*/
	{30, {0,		0,		0,		0}},
	{25, {3000,		1,		600,	12}},
	{20, {6000,		1,		3200,	12}},
	{15, {90000,	1,		5800,	12}},
	{12, {12000,	1,		8400,	12}},
	{10, {16000,	1,		11000,	12}},
	{8, {19000,		1,		13600,	12}},
	{5, {22000,		1,		16200,	12}},
};

/*default aec of fast boot*/
static struct ae_fast_struct ae_fast_default = {
	.sensor_exp_time = 852,
	.sensor_a_gain	= 580,
	.sensor_d_gain	= 256,
	.isp_d_gain		= 256,
	{
		.r_gain = 358,
		.g_gain = 256,
		.b_gain = 420,
		.r_offset = 0,
		.g_offset = 0,
		.b_offset = 0
	}
};

static AK_ISP_SENSOR_CB ar0230_cb;

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void ar0230_set_fps_async(struct ar0230_priv *priv);
static int __ar0230_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int ar0230_sensor_read_id_func(void *arg);   //no use IIC bus
static int ar0230_sensor_get_resolution_func(void *arg, int *width, int *height);
static int ar0230_sensor_set_fps_func(void *arg, const int fps);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 ar0230_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct ar0230_priv *to_ar0230(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct ar0230_priv,
			subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ar0230_priv, hdl)->subdev;
}

/*
 * XXXX_set_isp_timing -
 * set timing to isp capturer.
 * if fps changed, the isp timing may changed.
 *
 * some sensor not need change timing,
 * use timing from ipsconf file maybe fine
 */
static int ar0230_set_isp_timing(struct ar0230_priv *priv)
{
	int i;
	int ret = -1;
	int current_fps = priv->fps_info.current_fps;
	int pid = 0;

	for (i = 0; i < sizeof(fps_to_timing)/sizeof(fps_to_timing[0]); i++) {
		if ((current_fps == fps_to_timing[i].fps) ||
				(current_fps > fps_to_timing[i].fps)) {
			void *arg = priv->hcb.isp_timing_cb_info.isp_timing_arg;
			SET_ISP_MISC set_isp_timing = priv->hcb.isp_timing_cb_info.set_isp_timing;

			if (current_fps > fps_to_timing[i].fps)
				i = (i >= 1) ? i-1:0;

			pr_debug("set_isp_timing:%p, arg:%p, i:%d\n", set_isp_timing, arg, i);

			if (set_isp_timing)
				set_isp_timing(arg, &fps_to_timing[i].isp_timing);
			ret = 0;
			break;
		}
	}

	return ret;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int ar0230_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int ar0230_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
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
static int ar0230_s_ctrl(struct v4l2_ctrl *ctrl)
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
static int ar0230_core_s_power(struct v4l2_subdev *sd, int on)
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
static int ar0230_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = ar0230_sensor_read_id_func(NULL);   //no use IIC bus
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
static int ar0230_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ar0230_priv *priv = to_ar0230(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int ar0230_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	/*
	 * 	NOTE: 因为目前VTS(300A)不变，根据《AR0230CS-DG 20181225.pdf》
	 * 	Integration Time只跟frame_length_lines有关即R300A，
	 * 	如果保证R300A固定那么最大曝光值肯定是一样的.
	 *
	 * 	25 fps max exp_time=853
	 * 	之前V300已经使用最大曝光值853，沿用它。
	 */

	return 853;
}

/*
 * XXXX_core_g_ctrl -
 * get control
 * core functions
 *
 * @sd:				subdev
 * @ctrl:			pointer to ctrl
 */
static int ar0230_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = ar0230_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = ar0230_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = ar0230_get_max_exp_for_fps(sd, ctrl);
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
static int ar0230_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ar0230_priv *priv = to_ar0230(client);
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
static int ar0230_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ar0230_priv *priv = to_ar0230(client);
	int fps = ctrl->value;

	ar0230_sensor_set_fps_func(priv, fps);
	ar0230_set_fps_async(priv);
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
static int ar0230_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = ar0230_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = ar0230_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int ar0230_store_initial_regs(struct ar0230_priv *priv, void *arg)
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

static long ar0230_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ar0230_priv *priv = to_ar0230(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			ar0230_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = ar0230_get_max_exp_for_fps(sd, &ctrl);
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
static int ar0230_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct ar0230_priv *priv = to_ar0230(client);
	static struct ar0230_win_size win = {
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
static int ar0230_set_fmt(struct v4l2_subdev *sd,
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
static int ar0230_get_selection(struct v4l2_subdev *sd,
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
static int ar0230_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(ar0230_codes))
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
static int ar0230_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
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
static int ar0230_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	ar0230_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
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
static int ar0230_video_probe(struct i2c_client *client)
{
	struct ar0230_priv *priv = to_ar0230(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops ar0230_ctrl_ops = {
	.g_volatile_ctrl	= ar0230_g_volatile_ctrl,
	.s_ctrl				= ar0230_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops ar0230_subdev_core_ops = {
	.s_power	= ar0230_core_s_power,
	.g_ctrl		= ar0230_core_g_ctrl,
	.s_ctrl		= ar0230_core_s_ctrl,
	.ioctl		= ar0230_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int ar0230_g_mbus_config(struct v4l2_subdev *sd,
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
static int ar0230_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	pr_err("%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops ar0230_subdev_video_ops = {
	.s_stream	= ar0230_s_stream,
	.cropcap	= ar0230_cropcap,
	.g_crop		= ar0230_g_crop,
	.g_mbus_config	= ar0230_g_mbus_config,
	.s_crop		= ar0230_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops ar0230_subdev_pad_ops = {
	.enum_mbus_code = ar0230_enum_mbus_code,
	.get_fmt	= ar0230_get_fmt,
	.set_fmt	= ar0230_set_fmt,
	.get_selection	= ar0230_get_selection,
};

static struct v4l2_subdev_ops ar0230_subdev_ops = {
	.core	= &ar0230_subdev_core_ops,
	.video	= &ar0230_subdev_video_ops,
	.pad	= &ar0230_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct ar0230_priv *priv)
{
	struct device_node *np = client->dev.of_node;

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
static int ar0230_match(struct ar0230_priv *priv)
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
static int ar0230_probe(struct i2c_client *client,
		const struct i2c_device_id *did)
{
	struct ar0230_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
				"ar0230: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct ar0230_priv), GFP_KERNEL);
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
	priv->cb_info.cb = &ar0230_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!ar0230_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &ar0230_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &ar0230_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &ar0230_ctrl_ops,
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

	ret = ar0230_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "ar0230 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "ar0230 Probed success, subdev:%p\n", &priv->subdev);

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
static int ar0230_remove(struct i2c_client *client)
{
	struct ar0230_priv *priv = to_ar0230(client);

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

static const struct i2c_device_id ar0230_id[] = {
	{ "ar0230", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ar0230_id);

static const struct of_device_id ar0230_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, ar0230_of_match);

static struct i2c_driver ar0230_i2c_driver = {
	.driver = {
		.name = "ar0230",
		.of_match_table = of_match_ptr(ar0230_of_match),
	},
	.probe    = ar0230_probe,
	.remove   = ar0230_remove,
	.id_table = ar0230_id,
};

module_i2c_driver(ar0230_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for ar0230 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.6");

/*sensor i2c write*/
static int ar0230_write(const struct i2c_client *client, int reg, int value)
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
static int ar0230_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	pr_err("%s client:%p, addr:%x\n", __func__, client, client->addr);
	return i2c_read(&trans);
}

/*
 * The sensor must set callbacks
 *
 */
static int ar0230_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	int i;                         
	AK_ISP_SENSOR_REG_INFO *preg_info;  
	struct ar0230_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;    
	for (i = 0; i < para->num; i++) {
		if (DELAY_FLAG == preg_info->reg_addr)
			mdelay(100);
		else {
#if 0
			{
				int value;

				value = ar0230_read(client, preg_info->reg_addr);
				pr_err("reg:%x read value:%x\n", preg_info->reg_addr, value);
			}
#endif
			ar0230_write(client, preg_info->reg_addr, preg_info->value);
#if 0
			{
				int value;

				value = ar0230_read(client, preg_info->reg_addr);
				pr_err("reg:%x write value:%x, read back value:%x\n",
						preg_info->reg_addr, preg_info->value, value);
			}
#endif
		}

		preg_info++;
	}

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;
	priv->fps_info.to_fps_value = 0;

	ar0230_set_isp_timing(priv);
	return 0;
}

/*read sensor register*/
static int ar0230_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct ar0230_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return ar0230_read(client, reg_addr);
}
/*write sensor register*/
static int ar0230_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct ar0230_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return ar0230_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int ar0230_sensor_read_id_func(void *arg)   //no use IIC bus
{
	return SENSOR_ID;
}

static const unsigned short m_gain_tab[][3] = {
	/*reg_fine_gain  reg_corse_gain*/
	{11,0, 389},//1.52
	{12,0, 409},//1.60
	{13,0, 430},//1.68
	{14,0, 455},//1.78
	{15,0, 481},//1.88

	{0,	1, 512},//2.0
	{2,	1, 545},//2.13
	{4,	1, 586},//2.29
	{6,	1, 629},//2.46
	{8,	1, 683},//2.67

	{0, 0, 691},//2.70
	{1, 0, 714},//2.79
	{2, 0, 737},//2.88
	{3, 0, 762},//2.98

	{4,  0, 791},//3.09
	{5,  0, 819},//3.20
	{6,	0, 849},//3.32
	{7,	0, 885},//3.46
	{8,  0, 921},//3.60
	{9,	0, 962},//3.76
	{10,0, 1006},//3.93

	{11, 0, 1052},//4.11
	{12, 0, 1105},//4.32
	{13, 0, 1164},//4.55
	{14, 0, 1228},//4.80

	{15, 0, 1300},//5.08
	{0,  1, 1382},//5.40
	{2,  1, 1474},//5.76

	{4, 1, 1579},//6.17
	{6, 1, 1702},//6.65

	{8, 1, 1843},//7.20
	{10,1, 2012},//7.86

	{12,1, 2211},//8.64

	{14,1, 2457},//9.60
	{0, 2, 2764},//10.80
	{2, 2, 2949},//11.52

	{4, 2, 3159},//12.34
	{6, 2, 3402},//13.29

	{8,  2, 3886},//14.40
	{10, 2, 4021},//15.71
	{12, 2, 4423},//17.28
	{14, 2, 4915},//19.20
	{0,  3, 5529},//21.60
	{2,  3, 5900},//23.05
	{4,  3, 6320},//24.69
	{6,  3, 6807},//26.59
	{8,  3, 7370},//28.79
	{10, 3, 8046}//31.43
};

static unsigned int _cmos_gains_convert(struct i2c_client *client, unsigned int a_gain, unsigned int d_gain,
		unsigned int *a_gain_out ,unsigned int *d_gain_out)
{
	int i;
	int tmp;
	int flag;
	int index = 0;
	unsigned int fine_gain;
	unsigned int coarse_gain;

	for (i = 0; i < 48; i++) {
		if (a_gain < m_gain_tab[i][2]) {
			break;
		}

		index = i;
	}

	if (index >= 10)
		flag = 1;
	else
		flag = 0;
	fine_gain = m_gain_tab[index][0];
	coarse_gain = m_gain_tab[index][1];

	if (1 == flag) {
		ar0230_write(client, 0x3100, 0x4);
		/*HCG*/
		ar0230_write(client, 0x3096, 0x0780);
		ar0230_write(client, 0x3098, 0x0780);
		ar0230_write(client, 0x3202, 0x00b0);
		ar0230_write(client, 0x3206, 0x1c0e);
		ar0230_write(client, 0x3208, 0x4e39);
	} else {

		ar0230_write(client, 0x3100, 0x0);

		/*LCG*/
		ar0230_write(client, 0x3096, 0x0480);
		ar0230_write(client, 0x3098, 0x0480);
		ar0230_write(client, 0x3202, 0x0080);
		ar0230_write(client, 0x3206, 0x0b08);
		ar0230_write(client, 0x3208, 0x1e13);
	}

	/*set again*/
	tmp = ((fine_gain&0xf)<<0)|((coarse_gain&0x3)<<4);
	ar0230_write(client, 0x3060, tmp);

	return 0;
}

static int aec_parms_init(struct ar0230_priv *priv)
{
	return 0;
}

/*set sensor again*/
static int ar0230_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	struct ar0230_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned int tmp_a_gain;
	unsigned int tmp_d_gain;
	unsigned int tmp_a_gain_out;
	unsigned int tmp_d_gain_out;

	pr_debug("%s %d again:%u\n", __func__, __LINE__, a_gain);

	ar0230_set_fps_async(priv);

	tmp_a_gain = a_gain;
	tmp_d_gain = 0;

	_cmos_gains_convert(client, tmp_a_gain, tmp_d_gain,  &tmp_a_gain_out ,&tmp_d_gain_out);
	return A_GAIN_EFFECT_FRAMES;
}
/*set sensor dgain*/
static int ar0230_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	struct ar0230_priv *priv = arg;

	ar0230_set_fps_async(priv);
	return D_GAIN_EFFECT_FRAMES;
}
/*set sensor exp time*/
static int ar0230_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	/*
	 * 	NOTE: 因为目前VTS(300A)不变，根据《AR0230CS-DG 20181225.pdf》
	 * 	Integration Time只跟frame_length_lines有关即R300A，
	 * 	如果保证R300A固定那么最大曝光值肯定是一样的.
	 *
	 * 	25 fps max exp_time=853
	 * 	之前V300已经使用最大曝光值853，沿用它。
	 */

	struct ar0230_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned int exposure_time_ext = exp_time;
	unsigned int tem;

	pr_debug("%s %d exp:%u\n", __func__, __LINE__, exp_time);

	ar0230_set_fps_async(priv);

	exposure_time_ext = (exposure_time_ext&0xffff);
	tem = exposure_time_ext;

	if (1 > tem) {
		tem = 1;
	}

	if (tem >= 935) {
		ar0230_write(client, 0x2444, 0xf400);
		ar0230_write(client, 0x2446, 0x0001);
	} else {
		ar0230_write(client, 0x2444, 0x6000);
		ar0230_write(client, 0x2446, 0x0000);
	}

	ar0230_write(client, 0x3012, tem);

	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int ar0230_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int ar0230_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int ar0230_sensor_set_standby_out_func(void *arg)
{
	return 0;
}
/*low level read sensor ID, user i2c ops*/
static int __ar0230_sensor_probe_id_func(struct i2c_client *client)  //use IIC bus
{
	int i;
	int addr, value, rdvalue;
	unsigned short regs[] = {
		0x3006, 0x043f,
		0x3008, 0x078f,
		0x3010, 0xbeef
	};

	for (i = 0; i < sizeof(regs) / sizeof(regs[0]) / 2; i++) {
		addr = regs[i * 2];
		value = regs[i * 2 + 1];

		rdvalue = ar0230_read(client, addr);
		if (rdvalue != value) {
			pr_err("%s %d addr:0x%x, rdvalue:0x%x\n", __func__, __LINE__, addr, rdvalue);
		}
		pr_err("%s %d addr:0x%x, rdvalue:0x%x\n", __func__, __LINE__, addr, rdvalue);
	}

	pr_err("%s success\n", __func__);
	return SENSOR_ID;

fail:
	return 0;
}
/*read sensor ID, user i2c ops*/
static int ar0230_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct ar0230_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return __ar0230_sensor_probe_id_func(client);
}
/*get resolution*/
static int ar0230_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int ar0230_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int ar0230_sensor_get_fps_func(void *arg)
{
	struct ar0230_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int ar0230_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type ar0230_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}

static int get_ae_fast_default(struct ae_fast_struct *ae_fast)
{
	*ae_fast = ae_fast_default;
	return 0;
}

/*get self definded params*/
static int ar0230_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct ar0230_priv *priv = arg;

	switch (param) {
		case GET_INTERFACE:
			*((int *)value) = SENSOR_IO_INTERFACE;
			break;

		case GET_IO_LEVEL:
			*((int *)value) = SENSOR_IO_LEVEL;
			break;

		case GET_RESET_GPIO:
			*((int *)value) = priv->gpio_reset;
			break;

		case GET_PWDN_GPIO:
			*((int *)value) = priv->gpio_pwdn;
			break;

		case GET_AE_FAST_DEFAULT:
			get_ae_fast_default(value);
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int ar0230_sensor_set_power_on_func(void *arg)
{
	struct ar0230_priv *priv = arg;

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
static int ar0230_sensor_set_power_off_func(void *arg)
{
	struct ar0230_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}
/*set sensor fps*/
static int ar0230_sensor_set_fps_func(void *arg, const int fps)
{
	/*
	 * ar0230:
	 * HTS(0X300C)
	 * VTS(0X300A)
	 * fps = pclk / 2 / VTS / HTS
	 *
	 * the pclk=72MHz, VTS=0x500
	 *
	 * */
	int ret = 0;
	int tmp;
	struct ar0230_priv *priv = arg;

	pr_err("%s %d fps:%d\n", __func__, __LINE__, fps);

	switch (fps) {
		case 12:
			tmp = 1250;
			break;

		case 14:
			tmp = 1428;
			break;

		default:
			tmp = fps * 100;
			break;
	}

	tmp = 3600000000UL / 0x500 / tmp;

	if (!ret) {
		priv->fps_info.to_fps_value = tmp;
		priv->fps_info.to_fps = fps;
	}

	return 0;
}

static void ar0230_set_fps_async(struct ar0230_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (priv->fps_info.to_fps != priv->fps_info.current_fps) {
		ar0230_write(client, 0x300c, priv->fps_info.to_fps_value);
		priv->fps_info.current_fps = priv->fps_info.to_fps;

		ar0230_set_isp_timing(priv);
	}
}

static AK_ISP_SENSOR_CB ar0230_cb = {
	.sensor_init_func = ar0230_sensor_init_func,
	.sensor_read_reg_func = ar0230_sensor_read_reg_func,
	.sensor_write_reg_func = ar0230_sensor_write_reg_func,
	.sensor_read_id_func = ar0230_sensor_read_id_func,
	.sensor_update_a_gain_func = ar0230_sensor_update_a_gain_func,
	.sensor_update_d_gain_func = ar0230_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func = ar0230_sensor_updata_exp_time_func,
	.sensor_timer_func = ar0230_sensor_timer_func,
	.sensor_set_standby_in_func = ar0230_sensor_set_standby_in_func,
	.sensor_set_standby_out_func = ar0230_sensor_set_standby_out_func,
	.sensor_probe_id_func = ar0230_sensor_probe_id_func,
	.sensor_get_resolution_func = ar0230_sensor_get_resolution_func,
	.sensor_get_mclk_func = ar0230_sensor_get_mclk_func,
	.sensor_get_fps_func = ar0230_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func = ar0230_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func = ar0230_sensor_get_bus_type_func,
	.sensor_get_parameter_func = ar0230_sensor_get_parameter_func,
	.sensor_set_power_on_func = ar0230_sensor_set_power_on_func,
	.sensor_set_power_off_func = ar0230_sensor_set_power_off_func,
	.sensor_set_fps_func = ar0230_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return ar0230_sensor_read_id_func(NULL);
}

static char *sensor_if_func(void)
{
	static char ifstr[16] = "dvp"; 

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

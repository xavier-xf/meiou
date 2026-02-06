/*
 * mis5001 Camera Driver
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
#define SENSOR_I2C_ADDR			0x30
#define ACTUAL_SENSOR_ID		0x1311
#define SENSOR_ID				0x5001
#define SENSOR_MCLK				24
#define SENSOR_REGADDR_BYTE		2
#define SENSOR_DATA_BYTE		1
#define SENSOR_OUTPUT_WIDTH		2560
#define SENSOR_OUTPUT_HEIGHT	1440
#define SENSOR_VALID_OFFSET_X	0
#define SENSOR_VALID_OFFSET_Y	0
#define SENSOR_BUS_TYPE			BUS_TYPE_RAW
//#define SENSOR_IO_INTERFACE		MIPI_INTERFACE
#define SENSOR_IO_LEVEL			IO_LEVEL_1V8
#define MIPI_MHZ				596
#define MIPI_LANES				2
#define MAX_FPS					20
#define EXP_EFFECT_FRAMES		1	/*adjust exp_time for 1/2 frames*/
#define A_GAIN_EFFECT_FRAMES	1	/*adjust a_gain for every frames*/
#define D_GAIN_EFFECT_FRAMES	1	/*adjust d_gain for every frames*/

#define DELAY_FLAG		(0xffff)

/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

struct mis5001_win_size {
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
struct mis5001_aec_parms {
	int curr_again_level;
	int curr_again_10x;
	int r0x3101_value;
	int r0x3100_value;
	int target_exp_ctrl;
	int curr_2x_dgain;
	int curr_corse_gain;

	int default_hts;
	int calc_vts_tmp;
};

static const struct v4l2_ctrl_ops mis5001_ctrl_ops;
static const struct v4l2_ctrl_config config_sensor_get_id = {
	.ops	= &mis5001_ctrl_ops,
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
struct mis5001_priv {
	struct i2c_client *client;
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	const struct mis5001_win_size	*win;

	int gpio_reset;
	int gpio_pwdn;
	struct sensor_cb_info cb_info;
	struct sensor_fps_info fps_info;

	struct host_callbacks hcb;
	struct mis5001_aec_parms aec_parms;
    AK_ISP_SENSOR_INIT_PARA para;
    struct v4l2_ctrl *ctrl_sensor_get_id;
};

/*default aec of fast boot*/
static struct ae_fast_struct ae_fast_default = {
	.sensor_exp_time = 2152,
	.sensor_a_gain	= 256,
	.sensor_d_gain	= 256,
	.isp_d_gain		= 256,
	{
		.r_gain = 445,
		.g_gain = 256,
		.b_gain = 396,
		.r_offset = 0,
		.g_offset = 0,
		.b_offset = 0
	}
};

static AK_ISP_SENSOR_CB mis5001_cb;

static int dvp = 0;

module_param(dvp, int, 0644);

/*
 * check_id - check hardware sensor ID whether it meets this driver
 * 0-no check, force to meet
 * others-check, if not meet return fail
 */
static int check_id = 0;
module_param(check_id, int, 0644);

static void mis5001_set_fps_async(struct mis5001_priv *priv);
static int __mis5001_sensor_probe_id_func(struct i2c_client *client);  //use IIC bus
static int mis5001_sensor_read_id_func(void *arg);   //no use IIC bus
static int mis5001_sensor_get_resolution_func(void *arg, int *width, int *height);
static int mis5001_sensor_set_fps_func(void *arg, const int fps);
static int mis5001_fps_to_vts(struct mis5001_priv *priv, const int fps);
static int call_sensor_sys_init(void);
static int call_sensor_sys_deinit(void);

static u32 mis5001_codes[] = {
	MEDIA_BUS_FMT_YUYV8_2X8,
	MEDIA_BUS_FMT_UYVY8_2X8,
	MEDIA_BUS_FMT_RGB565_2X8_BE,
	MEDIA_BUS_FMT_RGB565_2X8_LE,
};

/*
 * General functions
 */
static struct mis5001_priv *to_mis5001(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct mis5001_priv,
			    subdev);
}

static struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct mis5001_priv, hdl)->subdev;
}

/*
 * XXXX_s_stream -
 * set steaming enable/disable
 * soc_camera_ops functions
 *
 * @sd:				subdev
 * @enable:			enable flags
 */
static int mis5001_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static int mis5001_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
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
static int mis5001_s_ctrl(struct v4l2_ctrl *ctrl)
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
static int mis5001_core_s_power(struct v4l2_subdev *sd, int on)
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
static int mis5001_get_sensor_id(struct v4l2_control *ctrl)
{
	ctrl->value = mis5001_sensor_read_id_func(NULL);   //no use IIC bus
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
static int mis5001_get_sensor_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mis5001_priv *priv = to_mis5001(client);

	ctrl->value = (int)&priv->cb_info;
	return 0;
}

static int mis5001_get_max_exp_for_fps(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mis5001_priv *priv = to_mis5001(client);
	int fps = ctrl->value;
	int vts = mis5001_fps_to_vts(priv, fps);

	ctrl->value = vts * 2 - 10;
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
static int mis5001_core_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case GET_SENSOR_ID:
			ret = mis5001_get_sensor_id(ctrl);
			break;

		case GET_SENSOR_CB:
			ret = mis5001_get_sensor_cb(sd, ctrl);
			break;

		case GET_MAX_EXP_FOR_FPS:
			ret = mis5001_get_max_exp_for_fps(sd, ctrl);
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
static int mis5001_set_isp_timing_cb(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mis5001_priv *priv = to_mis5001(client);
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
static int mis5001_set_fps_direct(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mis5001_priv *priv = to_mis5001(client);
	int fps = ctrl->value;

	mis5001_sensor_set_fps_func(priv, fps);
	mis5001_set_fps_async(priv);
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
static int mis5001_core_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret;

	switch (ctrl->id) {
		case SET_ISP_MISC_CALLBACK:
			ret = mis5001_set_isp_timing_cb(sd, ctrl);
			break;

		case SET_FPS_DIRECT:
			ret = mis5001_set_fps_direct(sd, ctrl);
			break;

		default:
			pr_err("%s cmd:%d not support\n", __func__, ctrl->id);
			ret = -1;
			break;
	}

	return ret;
}

static int mis5001_store_initial_regs(struct mis5001_priv *priv, void *arg)
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

static long mis5001_core_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct mis5001_priv *priv = to_mis5001(client);
	int ret = 0;

	pr_debug("%s\n", __func__);

	switch (cmd) {
		case AK_SENSOR_SET_INIT:
			mis5001_store_initial_regs(priv, arg);
			break;

		case AK_SENSOR_GET_MAX_EXP_FOR_FPS: {
			struct sensor_max_exp_for_fps *exp_for_fps = arg;
			struct v4l2_control ctrl;

			ctrl.value = exp_for_fps->fps;
			ret = mis5001_get_max_exp_for_fps(sd, &ctrl);
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
static int mis5001_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct mis5001_priv *priv = to_mis5001(client);
	static struct mis5001_win_size win = {
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
static int mis5001_set_fmt(struct v4l2_subdev *sd,
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
static int mis5001_get_selection(struct v4l2_subdev *sd,
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
static int mis5001_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(mis5001_codes))
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
static int mis5001_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
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
static int mis5001_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	mis5001_sensor_get_resolution_func(NULL, &a->bounds.width, &a->bounds.height);
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
static int mis5001_video_probe(struct i2c_client *client)
{
	struct mis5001_priv *priv = to_mis5001(client);
	int ret;

	ret = v4l2_ctrl_handler_setup(&priv->hdl);

	return ret;
}

/*ctrl ops*/
static const struct v4l2_ctrl_ops mis5001_ctrl_ops = {
	.g_volatile_ctrl	= mis5001_g_volatile_ctrl,
	.s_ctrl				= mis5001_s_ctrl,
};

/*core ops*/
static struct v4l2_subdev_core_ops mis5001_subdev_core_ops = {
	.s_power	= mis5001_core_s_power,
	.g_ctrl		= mis5001_core_g_ctrl,
	.s_ctrl		= mis5001_core_s_ctrl,
	.ioctl		= mis5001_core_ioctl,
};

/*
 * XXXX_g_mbus_config -
 * get buf config
 * video functions
 *
 * @sd:				subdev
 * @cfg:			return config
 */
static int mis5001_g_mbus_config(struct v4l2_subdev *sd,
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
static int mis5001_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	printk(KERN_ERR "%s %d, left:%d, top:%d, width:%d, height:%d\n",
			__func__, __LINE__, crop->c.left, crop->c.top, crop->c.width, crop->c.height);
	return 0;
}

/*video ops*/
static struct v4l2_subdev_video_ops mis5001_subdev_video_ops = {
	.s_stream	= mis5001_s_stream,
	.cropcap	= mis5001_cropcap,
	.g_crop		= mis5001_g_crop,
	.g_mbus_config	= mis5001_g_mbus_config,
	.s_crop		= mis5001_s_crop,
};

/*pad ops*/
static const struct v4l2_subdev_pad_ops mis5001_subdev_pad_ops = {
	.enum_mbus_code = mis5001_enum_mbus_code,
	.get_fmt	= mis5001_get_fmt,
	.set_fmt	= mis5001_set_fmt,
	.get_selection	= mis5001_get_selection,
};

/*sensor driver subdev ops*/
static struct v4l2_subdev_ops mis5001_subdev_ops = {
	.core	= &mis5001_subdev_core_ops,
	.video	= &mis5001_subdev_video_ops,
	.pad	= &mis5001_subdev_pad_ops,
};

/*
 * sensor_of_parse -
 * parse node of device
 *
 * @client:			pointor to i2c client
 * @priv:			sensor struct
 */
static int sensor_of_parse(struct i2c_client *client, struct mis5001_priv *priv)
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
static int mis5001_match(struct mis5001_priv *priv)
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
static int mis5001_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct mis5001_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int			ret;

	pr_err("%s %s %s\n", __func__, __DATE__, __TIME__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"mis5001: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct mis5001_priv), GFP_KERNEL);
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
	priv->cb_info.cb = &mis5001_cb;
	priv->cb_info.arg = priv;
	priv->client = client;

	if (!client->dev.of_node) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		ret = -EINVAL;
		goto err_clk;
	}

	if (!mis5001_match(priv)) {
		ret = -ENODEV;
		goto err_clk;
	}

	/*subdev init*/
	v4l2_i2c_subdev_init(&priv->subdev, client, &mis5001_subdev_ops);
	priv->subdev.flags |= /*V4L2_SUBDEV_FL_HAS_EVENTS | */V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	v4l2_ctrl_new_std(&priv->hdl, &mis5001_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&priv->hdl, &mis5001_ctrl_ops,
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

	ret = mis5001_video_probe(client);
	if (ret < 0) {
		dev_err(&client->dev, "OV2640 probe fail\n");
		goto err_videoprobe;
	}

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0) {
		dev_err(&client->dev, "v4l2 async register subdev fail, ret:%d\n", ret);
		goto err_videoprobe;
	}

	dev_err(&adapter->dev, "mis5001 Probed success, subdev:%p\n", &priv->subdev);

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
static int mis5001_remove(struct i2c_client *client)
{
	struct mis5001_priv       *priv = to_mis5001(client);

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

static const struct i2c_device_id mis5001_id[] = {
	{ "mis5001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mis5001_id);

static const struct of_device_id mis5001_of_match[] = {
	/*donot changed compatible, must the same as dts*/
	{.compatible = "anyka,sensor0", },
	{},
};
MODULE_DEVICE_TABLE(of, mis5001_of_match);

static struct i2c_driver mis5001_i2c_driver = {
	.driver = {
		.name = "mis5001",
		.of_match_table = of_match_ptr(mis5001_of_match),
	},
	.probe    = mis5001_probe,
	.remove   = mis5001_remove,
	.id_table = mis5001_id,
};

module_i2c_driver(mis5001_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for mis5001 sensor");
MODULE_AUTHOR("Anyka Microelectronic");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.4");

/*sensor i2c write*/
static int mis5001_write(const struct i2c_client *client, int reg, int value)
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
static int mis5001_read(const struct i2c_client *client, int reg)
{
	struct i2c_transfer_struct trans = {
		.client = client,
		.reg_bytes = SENSOR_REGADDR_BYTE,
		.value_bytes = SENSOR_DATA_BYTE,
		.reg = reg,
		.value = 0,
	};

	pr_debug("%s client:%p, addr:%x\n", __func__, client, client->addr);
	return i2c_read(&trans);
}

/*
 * The sensor must set callbacks
 *
 * */
static int mis5001_sensor_init_func(void *arg, const AK_ISP_SENSOR_INIT_PARA *npara)
{
	int i;
	AK_ISP_SENSOR_REG_INFO *preg_info;
	struct mis5001_priv *priv = arg;
	struct i2c_client *client = priv->client;
	AK_ISP_SENSOR_INIT_PARA *para = &priv->para;

	if (para->num <= 0 && npara)
		para = (void *)npara;

	preg_info = para->reg_info;
	for (i = 0; i < para->num; i++) {
#if 0
		{
			int value;

			value = mis5001_read(client, preg_info->reg_addr);
			pr_err("reg:%x read value:%x\n", preg_info->reg_addr, value);
		}
#endif
		mis5001_write(client, preg_info->reg_addr, preg_info->value);
#if 0
		{
			int value;

			value = mis5001_read(client, preg_info->reg_addr);
			pr_err("reg:%x write value:%x, read back value:%x\n",
					preg_info->reg_addr, preg_info->value, value);
		}
#endif

		preg_info++;
	}

	priv->fps_info.current_fps = MAX_FPS;
	priv->fps_info.to_fps = priv->fps_info.current_fps;
	// * FRAME_W_A(HTS) = R{0x310e, 0x310f} = 0xb76 = 2934
	priv->aec_parms.default_hts = ((mis5001_read(client, 0x310e)&0xff)<<8) | ((mis5001_read(client, 0x310f)&0xff)<<0); //(0x310c, 0x310d)
	// pr_err("0x310e:%d(0x%04x)", (mis5001_read(client, 0x310e)&0xff), (mis5001_read(client, 0x310e)&0xff));
	// pr_err("0x310f:%d(0x%04x)", (mis5001_read(client, 0x310f)&0xff), (mis5001_read(client, 0x310f)&0xff));
	// pr_err("priv->aec_parms.default_hts:%02d(0x%04x)", priv->aec_parms.default_hts, priv->aec_parms.default_hts);

	// * FRAME_H_A(VTS) = R{0x310c, 0x310d} = 0x5dc = 1500 at 20fps
	priv->fps_info.to_fps_value = ((mis5001_read(client, 0x310c)&0xff)<<8) | ((mis5001_read(client, 0x310d)&0xff)<<0); //(0x310c, 0x310d)
	// pr_err("0x310c:%d(0x%04x)", (mis5001_read(client, 0x310c)&0xff), (mis5001_read(client, 0x310c)&0xff));
	// pr_err("0x310d:%d(0x%04x)", (mis5001_read(client, 0x310d)&0xff), (mis5001_read(client, 0x310d)&0xff));
	// pr_err("priv->fps_info.to_fps_value:%02d(0x%04x)", priv->fps_info.to_fps_value, priv->fps_info.to_fps_value);

	priv->aec_parms.calc_vts_tmp  = (priv->aec_parms.default_hts * MAX_FPS * 100);
	return 0;
}

/*read sensor register*/
static int mis5001_sensor_read_reg_func(void *arg, const int reg_addr)
{
	struct mis5001_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return mis5001_read(client, reg_addr);
}
/*write sensor register*/
static int mis5001_sensor_write_reg_func(void *arg, const int reg_addr, int value)
{
	struct mis5001_priv *priv = arg;
	struct i2c_client *client = priv->client;

	return mis5001_write(client, reg_addr, value);
}
/*read sensor register, NO i2c ops*/
static int mis5001_sensor_read_id_func(void *arg)   //no use IIC bus
{
	return SENSOR_ID;
}

static int aec_parms_init(struct mis5001_priv *priv)
{
	priv->aec_parms.curr_again_level = 0;
	priv->aec_parms.curr_again_10x = -1;

	priv->aec_parms.r0x3101_value = 0;
	priv->aec_parms.r0x3100_value = 0;
	priv->aec_parms.target_exp_ctrl = 0;
	priv->aec_parms.curr_2x_dgain = 0;
	priv->aec_parms.curr_corse_gain = -1;

	priv->aec_parms.default_hts = 0;
	priv->aec_parms.calc_vts_tmp = 0;

	return 0;
}

/*set sensor again*/
static int mis5001_sensor_update_a_gain_func(void *arg, const unsigned int a_gain)
{
	/*
	 * max again < 16 times = 4096
	 * */
	struct mis5001_priv *priv = arg;
	struct i2c_client *client = priv->client;
	unsigned int corse_gain, fine_gain;

#if 1
	pr_debug("%s, again:%d\n", __func__, a_gain);
	if(a_gain < 512) {
		/*
		 * [1,2)
		 * 1/32
		 * */
		corse_gain = 0x0;
		fine_gain = (a_gain - 256) / 8; /*256/32=8*/
	} else if(a_gain < 1024) {
		/*
		 * [2,4)
		 * 1/32
		 * */
		corse_gain = 0x1;
		fine_gain = (a_gain - 512) / 16;/*512/32=16*/
	} else if(a_gain < 2048) {
		/*
		 * [4,8)
		 * 1/16
		 * */
		corse_gain = 0x2;
		fine_gain = (a_gain - 1024) / 32;/*1024/32=32*/
	} else if(a_gain < 4096) {
		/*
		 * [8,16)
		 * 1/8
		 * */
		corse_gain = 0x3;
		fine_gain = (a_gain - 2048) / 64;/*2048/32=64*/
	} else {
		corse_gain = 0x3;
		fine_gain = 0x1f;
	}

	mis5001_write(client, 0x3102, (corse_gain<<5)|(fine_gain&0x1f));
#endif

	return A_GAIN_EFFECT_FRAMES;
}
/*set sensor dgain*/
static int mis5001_sensor_update_d_gain_func(void *arg, const unsigned int d_gain)
{
	/*
	 * max dgain < 32 times = 8192
	 * */
	// struct mis5001_priv *priv = arg;
	// struct i2c_client *client = priv->client;
	// unsigned int corse_gain, fine_gain;

	// if (d_gain < 512) {
	// 	/*
	// 	 * [1,2)
	// 	 * 1/16
	// 	 * */
	// 	corse_gain = 0x0;
	// 	fine_gain = 0x80 + (((d_gain - 256) / 16) << 3); /*256/16=16*/
	// } else if (d_gain < 1024) {
	// 	/*
	// 	 * [2,4)
	// 	 * 1/16
	// 	 * */
	// 	corse_gain = 0x1;
	// 	fine_gain = 0x80 + (((d_gain - 512) / 32) << 3); /*512/16=32*/
	// } else if (d_gain < 2048) {
	// 	/*
	// 	 * [4,8)
	// 	 * 1/16
	// 	 * */
	// 	corse_gain = 0x3;
	// 	fine_gain = 0x80 + (((d_gain - 1024) / 64) << 3); /*1024/16=64*/
	// } else if (d_gain < 4096) {
	// 	/*
	// 	 * [8,16)
	// 	 * 1/16
	// 	 * */
	// 	corse_gain = 0x7;
	// 	fine_gain = 0x80 + (((d_gain - 2048) / 128) << 3); /*2048/16=128*/
	// } else {
	// 	corse_gain = 0xf;
	// 	fine_gain = 0xf8;
	// }

	// mis5001_write(client, 0x3e06, corse_gain&0xf);		//bit[3:0]
	// mis5001_write(client, 0x3e07, fine_gain);

	return D_GAIN_EFFECT_FRAMES;
}
static int __set_reg_exp_time(struct mis5001_priv *priv, int exp_time)
{
	int ret = 0;
	struct i2c_client *client = priv->client;

	unsigned char exposure_time_msb;
	unsigned char exposure_time_lsb;

	if(exp_time < 2) exp_time = 2;
	if(exp_time > 8191) exp_time = 8191;    //2^13
	pr_debug("%s, exp:%d\n", __func__, exp_time);

	exposure_time_msb =(exp_time>>8)&0xff;
	exposure_time_lsb =(exp_time>>0)&0xff;

	mis5001_write(client, 0x3100, exposure_time_msb);
	mis5001_write(client, 0x3101, exposure_time_lsb);

	priv->aec_parms.r0x3101_value = exposure_time_lsb;
	priv->aec_parms.r0x3100_value = exposure_time_msb;

	//priv->aec_parms.target_exp_ctrl = exp_time;
	return ret;
}

static int __set_reg_frame_vts(struct mis5001_priv *priv, int vts)
{
	int ret = 0;
	int vts_msb = 0;
	int vts_lsb = 0;

	struct i2c_client *client = priv->client;

	vts_msb = (vts >> 8) & 0xff;
	vts_lsb = vts & 0xff;
	// mis5001_write(client, 0x310c, vts_msb);
	// mis5001_write(client, 0x310d, vts_lsb);
	mis5001_write(client, 0x310e, vts_msb);
	mis5001_write(client, 0x310f, vts_lsb);

	pr_debug("%s %d vts:%d\n", __func__, __LINE__, vts);

	priv->fps_info.to_fps_value = vts;

	return ret;
}

static int __get_reg_frame_vts(struct mis5001_priv *priv)
{
	struct i2c_client *client = priv->client;

	// return mis5001_read(client, 0x310c) << 8 | mis5001_read(client, 0x310d);
	return mis5001_read(client, 0x310e) << 8 | mis5001_read(client, 0x310f);
}

static void update_frame_vts_and_exp_ctrl(struct mis5001_priv *priv)
{
	struct i2c_client *client = priv->client;

    /*
     * exp_time=0 mean one line exptime
     * max_exp_time = FRAME_H(VTS) - 5		//注：虽然手册中最大曝光是vts-1，但实际要vts-2，否则画面会异常。为了保证稳定性，这里使用vts-5。
     *              = R{0x310c, 0x310d} - 5
     * 
     * VTS = (0x310c,0x310d)
     *     = 0x5dc
     *     = 1500             //at 20fps
     * 
     * now:
     * max exp_time = FRAME_W(VTS) - 5
     *              = 1500 -5
     *              = 1495    //at 20fps
     */

	int const cur_frame_vts     = __get_reg_frame_vts(priv);
	int const target_frame_vts  = priv->fps_info.to_fps_value;

    int const max_exp_ctrl      = priv->fps_info.to_fps_value - 5;   //RAME_W(VTS) - 5

	int target_exp_ctrl = 0;
	if (priv->aec_parms.target_exp_ctrl <= max_exp_ctrl) {
		target_exp_ctrl = priv->aec_parms.target_exp_ctrl;
		// pr_err("priv->target_exp_ctrl <= max_exp_ctrl, use priv->aec_parms.target_exp_ctrl:%d\n",priv->aec_parms.target_exp_ctrl);
	} else {
		target_exp_ctrl = max_exp_ctrl;
		// pr_err("priv->target_exp_ctrl > max_exp_ctrl, use max_exp_ctrl:%d\n", max_exp_ctrl);
	}
    if(target_frame_vts <= 0)
        return ;
	// pr_err("VTS = %d->%d \n", cur_frame_vts, target_frame_vts);
	// pr_err("EXP = %d/%d \n", target_exp_ctrl, priv->aec_parms.target_exp_ctrl);

	if (target_frame_vts >= cur_frame_vts) {
		if (target_frame_vts != cur_frame_vts)
			__set_reg_frame_vts(priv, target_frame_vts);
		__set_reg_exp_time(priv, target_exp_ctrl);
	} else {
		__set_reg_exp_time(priv, target_exp_ctrl);
		__set_reg_frame_vts(priv, target_frame_vts);
	}
}

/*set sensor exp time*/
static int mis5001_sensor_updata_exp_time_func(void *arg, unsigned int exp_time)
{
	struct mis5001_priv *priv = arg;
	struct i2c_client *client = priv->client;

	priv->aec_parms.target_exp_ctrl = exp_time;
	update_frame_vts_and_exp_ctrl(priv);

	return EXP_EFFECT_FRAMES;
}
/*sensor timer*/
static int mis5001_sensor_timer_func(void *arg)
{
	return 0;
}
/*standby in*/
static int mis5001_sensor_set_standby_in_func(void *arg)
{
	return 0;
}
/*standby out*/
static int mis5001_sensor_set_standby_out_func(void *arg)
{
	return 0;
}

/*low level read sensor ID, user i2c ops*/
static int mis5001_sensor_probe_id_func(void *arg)  //use IIC bus
{
	struct mis5001_priv *priv = arg;
	struct i2c_client *client = priv->client;
	int id;
	int value;

	value = mis5001_read(client, 0x3000);
	id = value << 8;

	value = mis5001_read(client, 0x3001);
	id |= value;

	pr_err("id:%x\n", id);

	if (id == ACTUAL_SENSOR_ID)
		return SENSOR_ID;

	return 0;
}

/*get resolution*/
static int mis5001_sensor_get_resolution_func(void *arg, int *width, int *height)
{
	*width = SENSOR_OUTPUT_WIDTH;
	*height = SENSOR_OUTPUT_HEIGHT;
	return 0;
}
/*get mclk*/
static int mis5001_sensor_get_mclk_func(void *arg)
{
	return SENSOR_MCLK;
}
/*get current fps*/
static int mis5001_sensor_get_fps_func(void *arg)
{
	struct mis5001_priv *priv = arg;

	return priv->fps_info.current_fps;
}
/*get valid coordinate*/
static int mis5001_sensor_get_valid_coordinate_func(void *arg, int *x, int *y)
{
	*x = SENSOR_VALID_OFFSET_X;
	*y = SENSOR_VALID_OFFSET_Y;
	return 0;
}
/*get bus type*/
static enum sensor_bus_type mis5001_sensor_get_bus_type_func(void *arg)
{
	return SENSOR_BUS_TYPE;
}
static int get_ae_fast_default(struct ae_fast_struct *ae_fast)
{
	*ae_fast = ae_fast_default;
	return 0;
}
/*get self definded params*/
static int mis5001_sensor_get_parameter_func(void *arg, int param, void *value)
{
	int ret = 0;
	struct mis5001_priv *priv = arg;

	switch (param) {
		case GET_INTERFACE:
			*((int *)value) = dvp ? DVP_INTERFACE:MIPI_INTERFACE;
			break;

		case GET_IO_LEVEL:
			*((int *)value) = SENSOR_IO_LEVEL;
			break;

		case GET_MIPI_MHZ:
			*((int *)value) = MIPI_MHZ;
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

		case GET_AE_FAST_DEFAULT:
			get_ae_fast_default(value);
			break;

		case GET_SCAN_METHOD:
			ret = -1;
			break;

		default:
			pr_err("%s param:%d not support\n", __func__, param);
			ret = -1;
			break;
	}

	return ret;
}
/*sensor power on*/
static int mis5001_sensor_set_power_on_func(void *arg)
{
	struct mis5001_priv *priv = arg;

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
static int mis5001_sensor_set_power_off_func(void *arg)
{
	struct mis5001_priv *priv = arg;

	if (priv->gpio_pwdn >= 0) {
		gpio_direction_input(priv->gpio_pwdn);
	}

	if (priv->gpio_reset >= 0) {
		gpio_direction_input(priv->gpio_reset);
	}

	return 0;
}

static int mis5001_fps_to_vts(struct mis5001_priv *priv, const int fps)
{
	/*
	 * mis5001:
	 * MIS5001_Mclk24Mhz_Pclk88M_mipi2lane_raw12_4M@20fps.ini
	 * 
	 * mipi:
	 * now pclk=100000000hz
	 * MCLK=24Mhz,PCLK=88Mhz
	 * 
	 * FRAME_W_A(HTS) = R{0x310e, 0x310f} = 0xb76 = 2934
	 * FRAME_H_A(VTS) = R{0x310c, 0x310d} = 0x5dc = 1500 at 20fps
	 * 
	 * fps = PCLK/(FRAME_W(HTS)*FRAME_H(VTS))
	 * PCLK = FRAME_W(HTS)*FRAME_H(VTS)*fps
	 *      = 2934*1500*20
	 *      = 88020000UL
	 *
	 * FRAME_H(VTS) = PCLK/(FRAME_W(HTS)*fps)
	 *              = (88020000UL)/(2934)/fps
	 *              = (30000*100)/(fps*100)
	 *              = (3000000UL)/vts; 
	 * 
	 * or
	 * FRAME_W(HTS) = PCLK/(FRAME_H(VTS)*fps)
	 *              = (88020000UL)/(1500)/fps
	 *              = (58680*100)/(fps*100)
	 *              = (5868000UL)/vts; 
	 *
	 * */

	struct i2c_client *client = priv->client;

	int vts;

	pr_debug("%s %d fps:%d default_hts:%d tmp:%d\n", __func__, __LINE__, fps, priv->aec_parms.default_hts, priv->aec_parms.calc_vts_tmp);

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

	vts = (priv->aec_parms.calc_vts_tmp) / vts;
	// vts = (3000000UL) / vts;

	return vts;
}

/*set sensor fps*/
static int mis5001_sensor_set_fps_func(void *arg, const int fps)
{
	int tmp;
	struct mis5001_priv *priv = arg;

	tmp = mis5001_fps_to_vts(priv, fps);

	if (tmp > 0) {
		priv->fps_info.to_fps_value = tmp;
		priv->fps_info.to_fps = fps;
	}
	return 0;
}

static void mis5001_set_fps_async(struct mis5001_priv *priv)
{
	struct i2c_client *client = priv->client;

	if (priv->fps_info.to_fps != priv->fps_info.current_fps) {
		update_frame_vts_and_exp_ctrl(priv);
		priv->fps_info.current_fps = priv->fps_info.to_fps;
	}
}

static AK_ISP_SENSOR_CB mis5001_cb = {
	.sensor_init_func					= mis5001_sensor_init_func,
	.sensor_read_reg_func				= mis5001_sensor_read_reg_func,
	.sensor_write_reg_func				= mis5001_sensor_write_reg_func,
	.sensor_read_id_func				= mis5001_sensor_read_id_func,
	.sensor_update_a_gain_func			= mis5001_sensor_update_a_gain_func,
	.sensor_update_d_gain_func			= mis5001_sensor_update_d_gain_func,
	.sensor_updata_exp_time_func		= mis5001_sensor_updata_exp_time_func,
	.sensor_timer_func					= mis5001_sensor_timer_func,
	.sensor_set_standby_in_func			= mis5001_sensor_set_standby_in_func,
	.sensor_set_standby_out_func		= mis5001_sensor_set_standby_out_func,
	.sensor_probe_id_func				= mis5001_sensor_probe_id_func,
	.sensor_get_resolution_func			= mis5001_sensor_get_resolution_func,
	.sensor_get_mclk_func				= mis5001_sensor_get_mclk_func,
	.sensor_get_fps_func				= mis5001_sensor_get_fps_func,
	.sensor_get_valid_coordinate_func	= mis5001_sensor_get_valid_coordinate_func,
	.sensor_get_bus_type_func			= mis5001_sensor_get_bus_type_func,
	.sensor_get_parameter_func			= mis5001_sensor_get_parameter_func,
	.sensor_set_power_on_func			= mis5001_sensor_set_power_on_func,
	.sensor_set_power_off_func			= mis5001_sensor_set_power_off_func,
	.sensor_set_fps_func				= mis5001_sensor_set_fps_func,
};

static int sensor_id_func(void)
{
	return mis5001_sensor_read_id_func(NULL);
}

static char *sensor_if_func(void)
{
	static char ifstr[16] = "dvp";

	if (!dvp)
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

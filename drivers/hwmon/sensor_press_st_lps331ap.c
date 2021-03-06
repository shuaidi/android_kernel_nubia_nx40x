/*
* drivers/misc/lps331ap_prs.c
*
* STMicroelectronics LPS331AP Pressure / Temperature Sensor module driver
*
* Copyright (C) 2011 STMicroelectronics- MSH - Motion Mems BU - Application Team
* Matteo Dameno (matteo.dameno@st.com)
*
*
* Both authors are willing to be considered the contact and update points for
* the driver.
*
** Output data from the device are available from the assigned
* /dev/input/eventX device;
*
* LPS3311AP can be controlled by sysfs interface looking inside:
* /sys/bus/i2c/devices/<busnum>-<devaddr>/
*
* LPS331AP make available two i2C addresses selectable from platform_data
* by the LPS001WP_PRS_I2C_SAD_H or LPS001WP_PRS_I2C_SAD_L.
*
* Read pressures and temperatures output can be converted in units of
* measurement by dividing them respectively for SENSITIVITY_P and SENSITIVITY_T.
* Temperature values must then be added by the constant float TEMPERATURE_OFFSET
* expressed as Celsius degrees.
*
* Obtained values are then expessed as
* mbar (=0.1 kPa) and Celsius degrees.
*
* To use autozero feature you can write 0 zero or 1 to its corresponding sysfs
* file. This lets you to write current temperature and pressure into reference
* registers or to reset them.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
* 02110-1301 USA
*
*/
/******************************************************************************
 Revision 1.0.0 2011/Feb/14:
	first release
	moved to input/misc
 Revision 1.0.1 2011/Apr/04:
	xxx
 Revision 1.0.2 2011/Sep/01:
	corrects ord bug, forces BDU enable
 Revision 1.0.3 2011/Sep/15:
	introduces compansation params reading and sysfs file to get them
 Revision 1.0.4 2011/Dec/12:
	sets maximum allowable resolution modes dynamically with ODR;
 Revision 1.0.5 2012/Feb/29:
	introduces more compansation params and extends sysfs file content
	format to get them; reduces minimum polling period define;
	enables by default DELTA_EN bit1 on CTRL_REG1
	corrects bug on TSH acquisition
 Revision 1.0.6 2012/Mar/30:
 	introduces one more compansation param and extends sysfs file content
	format to get it.
 Revision 1.0.6.1 2012/Apr/12:
 	changes Resolution settings for 25Hz to TEMP AVG=128 and PRES AVG=384.
******************************************************************************/

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
//#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c/sensor_press_st_lps331ap.h>

#define	DEBUG_ON	

#define LOG_TAG "SENSOR_PRESS"
#ifdef  CONFIG_FEATURE_ZTEMT_SENSORS_LOG_ON
#define SENSOR_LOG_ERROR(fmt, args...) printk(KERN_ERR   "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
    #ifdef  DEBUG_ON
#define SENSOR_LOG_INFO(fmt, args...)  printk(KERN_INFO  "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
                                              
#define SENSOR_LOG_DEBUG(fmt, args...) printk(KERN_DEBUG "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
    #else
#define SENSOR_LOG_INFO(fmt, args...)
#define SENSOR_LOG_DEBUG(fmt, args...)
    #endif

#else
#define SENSOR_LOG_ERROR(fmt, args...)
#define SENSOR_LOG_INFO(fmt, args...)
#define SENSOR_LOG_DEBUG(fmt, args...)
#endif


#define	DEBUG	0


#define	PR_ABS_MAX	8388607		/* 24 bit 2'compl */
#define	PR_ABS_MIN	-8388608

#ifdef SHRT_MAX
#define	TEMP_MAX	SHRT_MAX
#define TEMP_MIN	SHRT_MIN
#else
#define	TEMP_MAX	SHORT_MAX
#define TEMP_MIN	SHORT_MIN
#endif

#define	WHOAMI_LPS331AP_PRS	0xBB	/*	Expctd content for WAI	*/

/*	CONTROL REGISTERS	*/
#define	REF_P_XL	0x08		/*	pressure reference	*/
#define	REF_P_L		0x09		/*	pressure reference	*/
#define	REF_P_H		0x0A		/*	pressure reference	*/
#define	REF_T_L		0x0B		/*	temperature reference	*/
#define	REF_T_H		0x0C		/*	temperature reference	*/


#define	WHO_AM_I	0x0F		/*	WhoAmI register		*/
#define	TP_RESOL	0x10		/*	Pres Temp resolution set*/
#define	DGAIN_L		0x18		/*	Dig Gain (3 regs)	*/

#define	CTRL_REG1	0x20		/*	power / ODR control reg	*/
#define	CTRL_REG2	0x21		/*	boot reg		*/
#define	CTRL_REG3	0x22		/*	interrupt control reg	*/
#define	INT_CFG_REG	0x23		/*	interrupt config reg	*/
#define	INT_SRC_REG	0x24		/*	interrupt source reg	*/
#define	THS_P_L		0x25		/*	pressure threshold	*/
#define	THS_P_H		0x26		/*	pressure threshold	*/
#define	STATUS_REG	0X27		/*	status reg		*/

#define	PRESS_OUT_XL	0x28		/*	press output (3 regs)	*/
#define	TEMP_OUT_L	0x2B		/*	temper output (2 regs)	*/
#define	COMPENS_L	0x30		/*	compensation reg (9 regs) */
#define	DELTA_T1	0x3B		/*	deltaTemp1 reg		 */
#define	DELTA_T2T3	0x3F		/*	deltaTemp2, deltaTemp3 reg */
#define CALIB_SETUP	0x1E		/*	calibrationSetup reg */
#define	CALIB_STP_MASK	0x80		/*	mask to catch calibSetup info */


/*	REGISTERS ALIASES	*/
#define	P_REF_INDATA_REG	REF_P_XL
#define	T_REF_INDATA_REG	REF_T_L
#define	P_THS_INDATA_REG	THS_P_L
#define	P_OUTDATA_REG		PRESS_OUT_XL
#define	T_OUTDATA_REG		TEMP_OUT_L
#define	OUTDATA_REG		PRESS_OUT_XL

/* */
#define	LPS331AP_PRS_ENABLE_MASK	0x80	/*  ctrl_reg1 */
#define	LPS331AP_PRS_ODR_MASK		0x70	/*  ctrl_reg1 */
#define	LPS331AP_PRS_DIFF_MASK		0x08	/*  ctrl_reg1 */
#define	LPS331AP_PRS_BDU_MASK		0x04	/*  ctrl_reg1 */
#define	LPS331AP_PRS_DELTA_EN_MASK	0x02	/*  ctrl_reg1 */
#define	LPS331AP_PRS_AUTOZ_MASK		0x02	/*  ctrl_reg2 */


#define	LPS331AP_PRS_PM_NORMAL		0x80	/* Power Normal Mode*/
#define	LPS331AP_PRS_PM_OFF		0x00	/* Power Down */

#define	LPS331AP_PRS_DIFF_ON		0x08	/* En Difference circuitry */
#define	LPS331AP_PRS_DIFF_OFF		0x00	/* Dis Difference circuitry */

#define	LPS331AP_PRS_AUTOZ_ON		0x02	/* En AutoZero Function */
#define	LPS331AP_PRS_AUTOZ_OFF		0x00	/* Dis Difference Function */

#define	LPS331AP_PRS_BDU_ON		0x04	/* En BDU Block Data Upd */
#define	LPS331AP_PRS_DELTA_EN_ON	0x02	/* En Delta Press registers */

#define	LPS331AP_PRS_RES_AVGTEMP_064	0X60
#define	LPS331AP_PRS_RES_AVGTEMP_128	0X70
#define	LPS331AP_PRS_RES_AVGPRES_512	0X0A
#define	LPS331AP_PRS_RES_AVGPRES_384	0X09

#define	LPS331AP_PRS_RES_MAX		(LPS331AP_PRS_RES_AVGTEMP_128  | \
						LPS331AP_PRS_RES_AVGPRES_512)
						/* Max Resol. for 1/7/12,5Hz */

#define	LPS331AP_PRS_RES_25HZ		(LPS331AP_PRS_RES_AVGTEMP_128  | \
						LPS331AP_PRS_RES_AVGPRES_384)
						/* Max Resol. for 25Hz */

#define	FUZZ			0
#define	FLAT			0

#define	I2C_AUTO_INCREMENT	0x80

/* RESUME STATE INDICES */
#define	LPS331AP_RES_REF_P_XL		0
#define	LPS331AP_RES_REF_P_L		1
#define	LPS331AP_RES_REF_P_H		2
#define	LPS331AP_RES_REF_T_L		3
#define	LPS331AP_RES_REF_T_H		4
#define	LPS331AP_RES_TP_RESOL		5
#define	LPS331AP_RES_CTRL_REG1		6
#define	LPS331AP_RES_CTRL_REG2		7
#define	LPS331AP_RES_CTRL_REG3		8
#define	LPS331AP_RES_INT_CFG_REG	9
#define	LPS331AP_RES_THS_P_L		10
#define	LPS331AP_RES_THS_P_H		11

#define	RESUME_ENTRIES			12
/* end RESUME STATE INDICES */

/* Pressure Sensor Operating Mode */
#define	LPS331AP_PRS_DIFF_ENABLE	1
#define LPS331AP_PRS_DIFF_DISABLE	0
#define	LPS331AP_PRS_AUTOZ_ENABLE	1
#define	LPS331AP_PRS_AUTOZ_DISABLE	0


static const struct {
	unsigned int cutoff_ms;
	unsigned int mask;
} lps331ap_prs_odr_table[] = {
	{40,	LPS331AP_PRS_ODR_25_25 },
	{80,	LPS331AP_PRS_ODR_12_12 },
	{143,	LPS331AP_PRS_ODR_7_7 },
	{1000,	LPS331AP_PRS_ODR_1_1 },
};

struct lps331ap_prs_data {
	struct i2c_client *client;
	struct lps331ap_prs_platform_data *pdata;

	struct mutex lock;
	struct delayed_work input_work;

	struct input_dev *input_dev;

	int hw_initialized;
	/* hw_working=-1 means not tested yet */
	int hw_working;


	atomic_t enabled;
	int on_before_suspend;

	u8 resume_state[RESUME_ENTRIES];

#if DEBUG
	u8 reg_addr;
#endif

	u32 TSL, TSH; 		// temperature points 1 - 2 - 3
	u32 TCV1, TCV2, TCV3;
	u32 TCS1, TCS2, TCS3;
	u32 digGain;
	s8 deltaT1, deltaT2, deltaT3;
	u8 testVer;
};

struct outputdata {
	s32 press;
	s16 temperature;
};

static int lps331ap_prs_i2c_read(struct lps331ap_prs_data *prs,
				  u8 *buf, int len)
{
	int err;
	struct i2c_msg msgs[] = {
		{
		 .addr = prs->client->addr,
		 .flags = prs->client->flags & I2C_M_TEN,
		 .len = 1,
		 .buf = buf,
		 }, {
		 .addr = prs->client->addr,
		 .flags = (prs->client->flags & I2C_M_TEN) | I2C_M_RD,
		 .len = len,
		 .buf = buf,
		 },
	};


	err = i2c_transfer(prs->client->adapter, msgs, 2);
	if (err != 2) 
    {
		SENSOR_LOG_ERROR("read transfer error\n");
		err = -EIO;
	}
	return 0;
}

static int lps331ap_prs_i2c_write(struct lps331ap_prs_data *prs,
				   u8 *buf, int len)
{
	int err;
	struct i2c_msg msgs[] = {
		{
		 .addr = prs->client->addr,
		 .flags = prs->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};

	err = i2c_transfer(prs->client->adapter, msgs, 1);
	if (err != 1) {
		SENSOR_LOG_ERROR( "write transfer error\n");
		err = -EIO;
	}
	return 0;
}


static int lps331ap_prs_register_write(struct lps331ap_prs_data *prs, u8 *buf,
		u8 reg_address, u8 new_value)
{
	int err;

	/* Sets configuration register at reg_address
	 *  NOTE: this is a straight overwrite  */
	buf[0] = reg_address;
	buf[1] = new_value;
	err = lps331ap_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		return err;
	return err;
}

static int lps331ap_prs_register_read(struct lps331ap_prs_data *prs, u8 *buf,
		u8 reg_address)
{
	int err;
	buf[0] = (reg_address);
	err = lps331ap_prs_i2c_read(prs, buf, 1);

	return err;
}

static int lps331ap_prs_register_update(struct lps331ap_prs_data *prs, u8 *buf,
		u8 reg_address, u8 mask, u8 new_bit_values)
{
	int err;
	u8 init_val;
	u8 updated_val;
	err = lps331ap_prs_register_read(prs, buf, reg_address);
	if (!(err < 0)) {
		init_val = buf[0];
		updated_val = ((mask & new_bit_values) | ((~mask) & init_val));
		err = lps331ap_prs_register_write(prs, buf, reg_address,
				updated_val);
	}
	return err;
}

static int lps331ap_prs_hw_init(struct lps331ap_prs_data *prs)
{
	int err;
	u8 buf[6];

	SENSOR_LOG_INFO("%s: hw init start\n", LPS331AP_PRS_DEV_NAME);

	buf[0] = WHO_AM_I;
	err = lps331ap_prs_i2c_read(prs, buf, 1);
	if (err < 0)
		goto error_firstread;
	else
		prs->hw_working = 1;
	if (buf[0] != WHOAMI_LPS331AP_PRS) {
		err = -1; /* TODO:choose the right coded error */
		goto error_unknown_device;
	}


	buf[0] = (I2C_AUTO_INCREMENT | P_REF_INDATA_REG);
	buf[1] = prs->resume_state[LPS331AP_RES_REF_P_XL];
	buf[2] = prs->resume_state[LPS331AP_RES_REF_P_L];
	buf[3] = prs->resume_state[LPS331AP_RES_REF_P_H];
	buf[4] = prs->resume_state[LPS331AP_RES_REF_T_L];
	buf[5] = prs->resume_state[LPS331AP_RES_REF_T_H];
	err = lps331ap_prs_i2c_write(prs, buf, 5);
	if (err < 0)
		goto err_resume_state;

	buf[0] = TP_RESOL;
	buf[1] = prs->resume_state[LPS331AP_RES_TP_RESOL];
	err = lps331ap_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = (I2C_AUTO_INCREMENT | P_THS_INDATA_REG);
	buf[1] = prs->resume_state[LPS331AP_RES_THS_P_L];
	buf[2] = prs->resume_state[LPS331AP_RES_THS_P_H];
	err = lps331ap_prs_i2c_write(prs, buf, 2);
	if (err < 0)
		goto err_resume_state;

	buf[0] = (I2C_AUTO_INCREMENT | CTRL_REG1);
	buf[1] = prs->resume_state[LPS331AP_RES_CTRL_REG1];
	buf[2] = prs->resume_state[LPS331AP_RES_CTRL_REG2];
	buf[3] = prs->resume_state[LPS331AP_RES_CTRL_REG3];
	err = lps331ap_prs_i2c_write(prs, buf, 3);
	if (err < 0)
		goto err_resume_state;

	buf[0] = INT_CFG_REG;
	buf[1] = prs->resume_state[LPS331AP_RES_INT_CFG_REG];
	err = lps331ap_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		goto err_resume_state;

	prs->hw_initialized = 1;
	SENSOR_LOG_INFO( "%s: hw init done\n", LPS331AP_PRS_DEV_NAME);
	return 0;

error_firstread:
	prs->hw_working = 0;
	SENSOR_LOG_ERROR("Error reading WHO_AM_I: is device "
		"available/working?\n");
	goto err_resume_state;
error_unknown_device:
	SENSOR_LOG_ERROR(
		"device unknown. Expected: 0x%02x,"
		" Replies: 0x%02x\n", WHOAMI_LPS331AP_PRS, buf[0]);
err_resume_state:
	prs->hw_initialized = 0;
	SENSOR_LOG_ERROR( "hw init error 0x%02x,0x%02x: %d\n", buf[0],
			buf[1], err);
	return err;
}

static void lps331ap_prs_device_power_off(struct lps331ap_prs_data *prs)
{
	int err;
	u8 buf[2] = { CTRL_REG1, LPS331AP_PRS_PM_OFF };

	err = lps331ap_prs_i2c_write(prs, buf, 1);
	if (err < 0)
		SENSOR_LOG_ERROR( "soft power off failed: %d\n", err);

	if (prs->pdata->power_off)
		prs->pdata->power_off();
	prs->hw_initialized = 0;
}

static int lps331ap_prs_device_power_on(struct lps331ap_prs_data *prs)
{
	int err = -1;

	if (prs->pdata->power_on) {
		err = prs->pdata->power_on();
		if (err < 0) {
			SENSOR_LOG_ERROR(
					"power_on failed: %d\n", err);
			return err;
		}
	}

	if (!prs->hw_initialized) {
		err = lps331ap_prs_hw_init(prs);
		if (prs->hw_working == 1 && err < 0) {
			lps331ap_prs_device_power_off(prs);
			return err;
		}
	}

	return 0;
}



int lps331ap_prs_update_odr(struct lps331ap_prs_data *prs, int poll_period_ms)
{
	int err = -1;
	int i;

	u8 buf[2];
	u8 init_val, updated_val;
	u8 curr_val, new_val;
	u8 mask = LPS331AP_PRS_ODR_MASK;
	u8 resol = LPS331AP_PRS_RES_MAX;

	/* Following, looks for the longest possible odr interval scrolling the
	 * odr_table vector from the end (longest period) backward (shortest
	 * period), to support the poll_interval requested by the system.
	 * It must be the longest period shorter then the set poll period.*/
	for (i = ARRAY_SIZE(lps331ap_prs_odr_table) - 1; i >= 0; i--) {
		if ((lps331ap_prs_odr_table[i].cutoff_ms <= poll_period_ms)
								|| (i == 0))
			break;
	}

	new_val = lps331ap_prs_odr_table[i].mask;
	if (new_val == LPS331AP_PRS_ODR_25_25)
		resol = LPS331AP_PRS_RES_25HZ;

	if (atomic_read(&prs->enabled)) {
		buf[0] = CTRL_REG1;
		err = lps331ap_prs_i2c_read(prs, buf, 1);
		if (err < 0)
			goto error;
		/* work on all but ENABLE bits */
		init_val = buf[0];
		prs->resume_state[LPS331AP_RES_CTRL_REG1] = init_val ;

		curr_val= ((LPS331AP_PRS_ENABLE_MASK & LPS331AP_PRS_PM_OFF)
				| ((~LPS331AP_PRS_ENABLE_MASK) & init_val));
		buf[0] = CTRL_REG1;
		buf[1] = curr_val;
		err = lps331ap_prs_i2c_write(prs, buf, 1);
		if (err < 0)
			goto error;

		buf[0] = CTRL_REG1;
		updated_val = ((mask & new_val) | ((~mask) & curr_val));

		buf[0] = CTRL_REG1;
		buf[1] = updated_val;
		err = lps331ap_prs_i2c_write(prs, buf, 1);
		if (err < 0)
			goto error;

		curr_val= ((LPS331AP_PRS_ENABLE_MASK &
						LPS331AP_PRS_PM_NORMAL)
			| ((~LPS331AP_PRS_ENABLE_MASK) & updated_val));
		buf[0] = CTRL_REG1;
		buf[1] = curr_val;
		err = lps331ap_prs_i2c_write(prs, buf, 1);
		if (err < 0)
			goto error;

		buf[0] = TP_RESOL;
		buf[1] = resol;
		err = lps331ap_prs_i2c_write(prs, buf, 1);
		if (err < 0)
			goto error;

		prs->resume_state[LPS331AP_RES_CTRL_REG1] = curr_val;
		prs->resume_state[LPS331AP_RES_TP_RESOL] = resol;
	}
	return err;

error:
	SENSOR_LOG_ERROR( "update odr failed 0x%02x,0x%02x: %d\n",
			buf[0], buf[1], err);

	return err;
}

static int lps331ap_prs_set_press_reference(struct lps331ap_prs_data *prs,
				s32 new_reference)
{
	int err;

	u8 bit_valuesXL,bit_valuesL, bit_valuesH;
	u8 buf[4];

	bit_valuesXL = (u8) (new_reference & 0x0000FF);
	bit_valuesL = (u8)((new_reference & 0x00FF00) >> 8);
	bit_valuesH = (u8)((new_reference & 0xFF0000) >> 16);


	buf[0] = (I2C_AUTO_INCREMENT | P_REF_INDATA_REG);
	buf[1] = bit_valuesXL;
	buf[2] = bit_valuesL;
	buf[3] = bit_valuesH;

	err = lps331ap_prs_i2c_write(prs,buf,3);
	if (err < 0)
		return err;

	prs->resume_state[LPS331AP_RES_REF_P_XL] = bit_valuesXL;
	prs->resume_state[LPS331AP_RES_REF_P_L] = bit_valuesL;
	prs->resume_state[LPS331AP_RES_REF_P_H] = bit_valuesH;

	return err;
}

static int lps331ap_prs_get_press_reference(struct lps331ap_prs_data *prs,
		s32 *buf32)
{
	int err;

	u8 bit_valuesXL, bit_valuesL, bit_valuesH;
	u8 buf[3];
	u16 temp = 0;

	buf[0] =  (I2C_AUTO_INCREMENT | P_REF_INDATA_REG);
	err = lps331ap_prs_i2c_read(prs, buf, 3);
	if (err < 0)
		return err;
	bit_valuesXL = buf[0];
	bit_valuesL = buf[1];
	bit_valuesH = buf[2];


	temp = (( bit_valuesH ) << 8 ) | ( bit_valuesL ) ;
	*buf32 = (s32)((((s16) temp) << 8) | ( bit_valuesXL ));
#if DEBUG
	SENSOR_LOG_INFO("%s val: %+d", LPS331AP_PRS_DEV_NAME, *buf32 );
#endif
	return err;
}

static int lps331ap_prs_set_temperature_reference(struct lps331ap_prs_data *prs,
				s16 new_reference)
{
	int err;
	u8 bit_valuesL, bit_valuesH;
	u8 buf[3];

	bit_valuesL = (u8) ( new_reference & 0x00FF );
	bit_valuesH = (u8)(( new_reference & 0xFF00 ) >> 8);

	buf[0] = (I2C_AUTO_INCREMENT | T_REF_INDATA_REG);
	buf[1] = bit_valuesL;
	buf[2] = bit_valuesH;
	err = lps331ap_prs_i2c_write(prs,buf,2);

	if (err < 0)
		return err;

	prs->resume_state[LPS331AP_RES_REF_T_L] = bit_valuesL;
	prs->resume_state[LPS331AP_RES_REF_T_H] = bit_valuesH;
	return err;
}

static int lps331ap_prs_get_temperature_reference(struct lps331ap_prs_data *prs,
		s16 *buf16)
{
	int err;

	u8 bit_valuesL, bit_valuesH;
	u8 buf[2] = {0};
	u16 temp = 0;


	buf[0] =  (I2C_AUTO_INCREMENT | T_REF_INDATA_REG);
	err = lps331ap_prs_i2c_read(prs, buf, 2);
	if (err < 0)
		return err;

	bit_valuesL = buf[0];
	bit_valuesH = buf[1];

	temp = ( ( (u16) bit_valuesH  ) << 8 );
	*buf16 = (s16)( temp | ((u16) bit_valuesL ));

	return err;
}


static int lps331ap_prs_autozero_manage(struct lps331ap_prs_data *prs,
								u8 control)
{
	int err;
	u8 buf[6];
	u8 const mask = LPS331AP_PRS_AUTOZ_MASK;
	u8 bit_values = LPS331AP_PRS_AUTOZ_OFF;
	u8 init_val;

	if (control >= LPS331AP_PRS_AUTOZ_ENABLE){
		bit_values = LPS331AP_PRS_AUTOZ_ON;



		buf[0] = CTRL_REG2;
		err = lps331ap_prs_i2c_read(prs, buf,1);
		if (err < 0)
			return err;

		init_val = buf[0];
		prs->resume_state[LPS331AP_RES_CTRL_REG2] = init_val;

		err = lps331ap_prs_register_update(prs, buf, CTRL_REG2,
					mask, bit_values);
		if (err < 0)
			return err;
	}
	else
	{
		buf[0] = (I2C_AUTO_INCREMENT | P_REF_INDATA_REG);
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		buf[4] = 0;
		buf[5] = 0;
		err = lps331ap_prs_i2c_write(prs, buf, 5);
		if (err < 0)
			return err;
		prs->resume_state[LPS331AP_RES_REF_P_XL] = 0;
		prs->resume_state[LPS331AP_RES_REF_P_L] = 0;
		prs->resume_state[LPS331AP_RES_REF_P_H] = 0;
		prs->resume_state[LPS331AP_RES_REF_T_L] = 0;
		prs->resume_state[LPS331AP_RES_REF_T_H] = 0;
	}
	return 0;
}


static int lps331ap_prs_get_presstemp_data(struct lps331ap_prs_data *prs,
						struct outputdata *out)
{
	int err;
	/* Data bytes from hardware	PRESS_OUT_XL,PRESS_OUT_L,PRESS_OUT_H, */
	/*				TEMP_OUT_L, TEMP_OUT_H */

	u8 prs_data[5];

	s32 pressure;
	s16 temperature;

	int regToRead = 5;

	prs_data[0] = (I2C_AUTO_INCREMENT | OUTDATA_REG);
	err = lps331ap_prs_i2c_read(prs, prs_data, regToRead);
	if (err < 0)
		return err;

#if DEBUG
	SENSOR_LOG_INFO("temp out tH = 0x%02x, tL = 0x%02x,"
			        "press_out: pH = 0x%02x, pL = 0x%02x, pXL= 0x%02x\n",
					prs_data[4],
					prs_data[3],
					prs_data[2],
					prs_data[1],
					prs_data[0]);
#endif

	pressure = (s32)((((s8) prs_data[2]) << 16) |
				(prs_data[1] <<  8) |
						( prs_data[0]));
	temperature = (s16) ((((s8) prs_data[4]) << 8) | (prs_data[3]));

	out->press = pressure;
	out->temperature = temperature;

	return err;
}

static int lps331ap_prs_acquire_compensation_data(struct lps331ap_prs_data *prs)
{
	int err;
	/* Data bytes from hardware	PRESS_OUT_XL,PRESS_OUT_L,PRESS_OUT_H, */
	/*				TEMP_OUT_L, TEMP_OUT_H */

	u8 compens_data[10];
	u8 gain_data[3];
	u8 delta_data[2];
	u8 dT1, dT23;
	u8 calSetup;

	int regToRead = 10;

	compens_data[0] = (I2C_AUTO_INCREMENT | COMPENS_L);
	err = lps331ap_prs_i2c_read(prs, compens_data, regToRead);
	if (err < 0)
		return err;

	regToRead = 3;
	gain_data[0] = (I2C_AUTO_INCREMENT | DGAIN_L);
	err = lps331ap_prs_i2c_read(prs, gain_data, regToRead);
	if (err < 0)
		return err;

	regToRead = 1;
	delta_data[0] = ( DELTA_T1 );
	err = lps331ap_prs_i2c_read(prs, delta_data, regToRead);
	if (err < 0)
		return err;
	dT1 = delta_data[0];

	regToRead = 1;
	delta_data[0] = ( DELTA_T2T3 );
	err = lps331ap_prs_i2c_read(prs, delta_data, regToRead);
	if (err < 0)
		return err;
	dT23 = delta_data[0];

	regToRead = 1;
	delta_data[0] = ( CALIB_SETUP );
	err = lps331ap_prs_i2c_read(prs, delta_data, regToRead);
	if (err < 0)
		return err;
	calSetup = delta_data[0];

#if DEBUG
	//dT1  = 0xD1;
	//dT23 = 0x20;
	//calSetup = 0x80;
	//dev_info(&prs_client->dev, "forced registers 0x3b, 0x3f, 0x1e"
	//						" values for debug\n");
#endif

#if DEBUG

	SENSOR_LOG_INFO("reg\n 0x30 = 0x%02x\n 0x31 = 0x%02x\n "
                "0x32 = 0x%02x\n 0x33 = 0x%02x\n 0x34 = 0x%02x\n "
                "0x35 = 0x%02x\n 0x36 = 0x%02x\n 0x37 = 0x%02x\n "
                "0x38 = 0x%02x\n 0x39 = 0x%02x\n",
                compens_data[0],
                compens_data[1],
                compens_data[2],
                compens_data[3],
                compens_data[4],
                compens_data[5],
                compens_data[6],
                compens_data[7],
                compens_data[8],
                compens_data[9]
		);

	SENSOR_LOG_INFO(
			"reg\n 0x18 = 0x%02x\n 0x19 = 0x%02x\n 0x1A = 0x%02x\n",
			gain_data[0],
			gain_data[1],
			gain_data[2]
		);

	SENSOR_LOG_INFO(
			"reg\n 0x3b = 0x%02x\n 0x3f = 0x%02x\n 0x1e = 0x%02x\n",
			dT1,
			dT23,
			calSetup
		);

#endif

	prs->TSL = (u16) (( compens_data[0] & 0xFC ) >> 2);

	prs->TSH = (u16) ( compens_data[1] & 0x3F );

	prs->TCV1 = (u32) (((( compens_data[3] & 0x03) << 16 ) |
				(( compens_data[2] & 0xFF) << 8 ) |
					( compens_data[1] & 0xC0 )) >> 6);

	prs->TCV2 = (u32) (((( compens_data[4] & 0x3F) << 8 ) |
					( compens_data[3] & 0xFC )) >> 2);

	prs->TCV3 = (u32) (((( compens_data[6] & 0x03) << 16 ) |
				(( compens_data[5] & 0xFF) << 8 ) |
					( compens_data[4] & 0xC0 )) >> 6);



	prs->TCS1 = (u32) (((( compens_data[7] & 0x0F) << 8 ) |
					( compens_data[6] & 0xFC )) >> 2);

	prs->TCS2 = (u32) (((( compens_data[8] & 0x3F) << 8 ) |
					( compens_data[7] & 0xF0 )) >> 4);

	prs->TCS3 = (u32) (((( compens_data[9] & 0xFF) << 8 ) |
					( compens_data[8] & 0xC0 )) >> 6);

	prs->digGain = (u32) (((( gain_data[2] & 0x0F) << 16 ) |
				(( gain_data[1] & 0xFF) << 8 ) |
					( gain_data[0] & 0xFC )) >> 2);
#if DEBUG
	//dT1 = 0xE0;
	SENSOR_LOG_INFO("test dT1 = 0x%08x\n",dT1);
#endif
	prs->deltaT1 = (((s8)( dT1 & 0xF0)) >> 4);
#if DEBUG
	SENSOR_LOG_INFO("test deltaT1 = 0x%08x\n",prs->deltaT1);

	//dT23 = 0xE0;
	SENSOR_LOG_INFO("test dT23 = 0x%08x\n",dT23);
#endif
	prs->deltaT2 = (((s8)( dT23 & 0xF0)) >> 4);
#if DEBUG
	SENSOR_LOG_INFO("test deltaT2 = 0x%08x\n",prs->deltaT2);

	//dT23 = 0x0E;
	SENSOR_LOG_INFO("test dT23 = 0x%08x\n",dT23);
#endif
	prs->deltaT3 = (((s8)(( dT23 & 0x0F) << 4)) >>4);
#if DEBUG
	SENSOR_LOG_INFO( "test deltaT3 = 0x%08x\n",prs->deltaT3);

	//calSetup = 0xe0;
	SENSOR_LOG_INFO("test calSetup = 0x%08x\n",calSetup);
#endif
	prs->testVer = ((calSetup & CALIB_STP_MASK) > 0)  ? 1 : 0;

	SENSOR_LOG_INFO("reg TSL = %d, TSH = %d,"
			" TCV1 = %d, TCV2 = %d, TCV3 = %d,"
			" TCS1 = %d, TCS2 = %d, TCS3 = %d,"
			" DGAIN = %d,"
			" deltaT1 = %d, deltaT2 = %d,"
			" deltaT3 = %d,"
			" testVer = %d\n",
			prs->TSL,
			prs->TSH,
			prs->TCV1,
			prs->TCV2,
			prs->TCV3,
			prs->TCS1,
			prs->TCS2,
			prs->TCS3,
			prs->digGain,
			prs->deltaT1,
			prs->deltaT2,
			prs->deltaT3,
			prs->testVer
		);



	return err;
}


static void lps331ap_prs_report_values(struct lps331ap_prs_data *prs,
					struct outputdata *out)
{

	input_report_abs(prs->input_dev, ABS_PR, out->press);

	input_report_abs(prs->input_dev, ABS_TEMP, out->temperature);
	input_sync(prs->input_dev);
}

static int lps331ap_prs_enable(struct lps331ap_prs_data *prs)
{
	int err;

	if (!atomic_cmpxchg(&prs->enabled, 0, 1)) {
		err = lps331ap_prs_device_power_on(prs);
		if (err < 0) {
			atomic_set(&prs->enabled, 0);
			return err;
		}
		schedule_delayed_work(&prs->input_work,
			msecs_to_jiffies(prs->pdata->poll_interval));
	}

	return 0;
}

static int lps331ap_prs_disable(struct lps331ap_prs_data *prs)
{
	if (atomic_cmpxchg(&prs->enabled, 1, 0)) {
		cancel_delayed_work_sync(&prs->input_work);
		lps331ap_prs_device_power_off(prs);
	}

	return 0;
}


static ssize_t attr_get_polling_rate(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	int val;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	mutex_lock(&prs->lock);
	val = prs->pdata->poll_interval;
	mutex_unlock(&prs->lock);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_polling_rate(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	unsigned long interval_ms;

	if (strict_strtoul(buf, 10, &interval_ms))
		return -EINVAL;
	if (!interval_ms)
		return -EINVAL;
	interval_ms = max((unsigned int)interval_ms,prs->pdata->min_interval);
	mutex_lock(&prs->lock);
	prs->pdata->poll_interval = interval_ms;
	lps331ap_prs_update_odr(prs, interval_ms);
	mutex_unlock(&prs->lock);
	return size;
}


static ssize_t attr_get_enable(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	int val = atomic_read(&prs->enabled);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		lps331ap_prs_enable(prs);
	else
		lps331ap_prs_disable(prs);

	return size;
}

static ssize_t attr_get_press_ref(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int err;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	s32 val = 0;

	mutex_lock(&prs->lock);
	err = lps331ap_prs_get_press_reference(prs, &val);
	mutex_unlock(&prs->lock);
	if (err < 0)
		return err;

	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_press_ref(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int err = -1;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	long val = 0;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	if (val < PR_ABS_MIN || val > PR_ABS_MAX)
		return -EINVAL;


	mutex_lock(&prs->lock);
	err = lps331ap_prs_set_press_reference(prs, val);
	mutex_unlock(&prs->lock);
	if (err < 0)
		return err;
	return size;
}

static ssize_t attr_get_temperature_ref(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int err;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	s16 val = 0;

	mutex_lock(&prs->lock);
	err = lps331ap_prs_get_temperature_reference(prs, &val);
	mutex_unlock(&prs->lock);
	if (err < 0 )
		return err;

	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_temperature_ref(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	int err = -1;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	long val=0;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;


	if ( val< TEMP_MIN || val> TEMP_MAX)
		return -EINVAL;


	mutex_lock(&prs->lock);
	err = lps331ap_prs_set_temperature_reference(prs, val);
	mutex_unlock(&prs->lock);
	if (err < 0)
		return err;
	return size;
}



static ssize_t attr_set_autozero(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t size)
{
	int err;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&prs->lock);
	err = lps331ap_prs_autozero_manage(prs, (u8) val);
	mutex_unlock(&prs->lock);
	if (err < 0)
		return err;
	return size;
}

static ssize_t attr_get_compensation_param(struct device *dev,
				struct device_attribute *attr, char *buf)
{

	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	return sprintf(buf,
			"%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
			prs->TSL,
			prs->TSH,
			prs->TCV1,
			prs->TCV2,
			prs->TCV3,
			prs->TCS1,
			prs->TCS2,
			prs->TCS3,
			prs->digGain,
			prs->deltaT1,
			prs->deltaT2,
			prs->deltaT3,
			prs->testVer
	);
}

#if DEBUG
static ssize_t attr_reg_set(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	int rc;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	u8 x[2];
	unsigned long val;

	if (strict_strtoul(buf, 16, &val))
		return -EINVAL;
	mutex_lock(&prs->lock);
	x[0] = prs->reg_addr;
	mutex_unlock(&prs->lock);
	x[1] = val;
	rc = lps331ap_prs_i2c_write(prs, x, 1);
	/*TODO: error need to be managed */
	return size;
}

static ssize_t attr_reg_get(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	ssize_t ret;
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	int rc;
	u8 data;

	mutex_lock(&prs->lock);
	data = prs->reg_addr;
	mutex_unlock(&prs->lock);
	rc = lps331ap_prs_i2c_read(prs, &data, 1);
	/*TODO: error need to be managed */
	ret = sprintf(buf, "0x%02x\n", data);
	return ret;
}

static ssize_t attr_addr_set(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct lps331ap_prs_data *prs = dev_get_drvdata(dev);
	unsigned long val;
	if (strict_strtoul(buf, 16, &val))
		return -EINVAL;
	mutex_lock(&prs->lock);
	prs->reg_addr = val;
	mutex_unlock(&prs->lock);
	return size;
}
#endif



static struct device_attribute attributes[] = {
	__ATTR(pollrate_ms,                 0664,   attr_get_polling_rate,          attr_set_polling_rate),
	__ATTR(enable_device,               0664,   attr_get_enable,                attr_set_enable),
	__ATTR(pressure_reference_level,    0664,   attr_get_press_ref,             attr_set_press_ref),
	__ATTR(temperature_reference_level, 0664,   attr_get_temperature_ref,       attr_set_temperature_ref),
	__ATTR(enable_autozero,             0220,   NULL,                           attr_set_autozero),
	__ATTR(compensation_param,          0444,   attr_get_compensation_param,    NULL),
#if DEBUG
	__ATTR(reg_value,                   0664,   attr_reg_get,                   attr_reg_set),
	__ATTR(reg_addr,                    0220,   NULL,                           attr_addr_set),
#endif
};

static int create_sysfs_interfaces(struct device *dev)
{
	int i   = 0;
    int ret = 0;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		ret = device_create_file(dev, attributes + i);
		if (ret < 0)
			goto error;
	return 0;

error:
	for ( ; i >= 0; i--)
		device_remove_file(dev, attributes + i);
	SENSOR_LOG_ERROR("%s:Unable to create interface\n", __func__);
	return ret;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		device_remove_file(dev, attributes + i);
}


static void lps331ap_prs_input_work_func(struct work_struct *work)
{
	struct lps331ap_prs_data *prs = container_of(
			(struct delayed_work *)work,
			struct lps331ap_prs_data,
			input_work);

	static struct outputdata output;
	int err;

	mutex_lock(&prs->lock);
	err = lps331ap_prs_get_presstemp_data(prs, &output);
	if (err < 0)
		SENSOR_LOG_ERROR( "get_pressure_data failed\n");
	else
		lps331ap_prs_report_values(prs, &output);

	schedule_delayed_work(&prs->input_work,
				msecs_to_jiffies(prs->pdata->poll_interval));
	mutex_unlock(&prs->lock);
}

int lps331ap_prs_input_open(struct input_dev *input)
{
	struct lps331ap_prs_data *prs = input_get_drvdata(input);

	return lps331ap_prs_enable(prs);
}

void lps331ap_prs_input_close(struct input_dev *dev)
{
	lps331ap_prs_disable(input_get_drvdata(dev));
}


static int lps331ap_prs_validate_pdata(struct lps331ap_prs_data *prs)
{
	/* checks for correctness of minimal polling period */
	prs->pdata->min_interval =
		max((unsigned int)LPS331AP_PRS_MIN_POLL_PERIOD_MS,
						prs->pdata->min_interval);

	prs->pdata->poll_interval = max(prs->pdata->poll_interval,
					prs->pdata->min_interval);

	/* Checks polling interval relative to minimum polling interval */
	if (prs->pdata->poll_interval < prs->pdata->min_interval) {
		SENSOR_LOG_ERROR( "minimum poll interval violated\n");
		return -EINVAL;
	}

	return 0;
}

static int lps331ap_prs_input_init(struct lps331ap_prs_data *prs)
{
	int err;

	INIT_DELAYED_WORK(&prs->input_work, lps331ap_prs_input_work_func);
	prs->input_dev = input_allocate_device();
	if (!prs->input_dev) {
		err = -ENOMEM;
		SENSOR_LOG_ERROR( "input device allocate failed\n");
		goto err0;
	}

	prs->input_dev->open = lps331ap_prs_input_open;
	prs->input_dev->close = lps331ap_prs_input_close;
	prs->input_dev->name = LPS331AP_PRS_DEV_NAME;
	prs->input_dev->id.bustype = BUS_I2C;
	prs->input_dev->dev.parent = &prs->client->dev;

	input_set_drvdata(prs->input_dev, prs);

	set_bit(EV_ABS, prs->input_dev->evbit);

	input_set_abs_params(prs->input_dev, ABS_PR,
			PR_ABS_MIN, PR_ABS_MAX, FUZZ, FLAT);
	input_set_abs_params(prs->input_dev, ABS_TEMP,
			TEMP_MIN, TEMP_MAX, FUZZ, FLAT);


	//prs->input_dev->name = "LPS331AP barometer";

	err = input_register_device(prs->input_dev);
	if (err) {
		SENSOR_LOG_ERROR(
			"unable to register input polled device %s\n",
			prs->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(prs->input_dev);
err0:
	return err;
}

static void lps331ap_prs_input_cleanup(struct lps331ap_prs_data *prs)
{
	input_unregister_device(prs->input_dev);
	/* input_free_device(prs->input_dev);*/
}

static int lps331ap_prs_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct lps331ap_prs_data *prs;
	int err = -1;
	int tempvalue;

	SENSOR_LOG_INFO("%s: probe start.\n", LPS331AP_PRS_DEV_NAME);

	if (client->dev.platform_data == NULL) {
		SENSOR_LOG_ERROR("platform data is NULL. exiting.\n");
		err = -ENODATA;
		goto err_exit_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		SENSOR_LOG_ERROR("client not i2c capable\n");
		err = -ENODEV;
		goto err_exit_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE |
					I2C_FUNC_SMBUS_BYTE_DATA |
					I2C_FUNC_SMBUS_WORD_DATA)) {
		SENSOR_LOG_ERROR("client not smb-i2c capable:2\n");
		err = -EIO;
		goto err_exit_check_functionality_failed;
	}


	if (!i2c_check_functionality(client->adapter,
						I2C_FUNC_SMBUS_I2C_BLOCK)){
		SENSOR_LOG_ERROR("client not smb-i2c capable:3\n");
		err = -EIO;
		goto err_exit_check_functionality_failed;
	}


	prs = kzalloc(sizeof(struct lps331ap_prs_data), GFP_KERNEL);
	if (prs == NULL) {
		err = -ENOMEM;
		SENSOR_LOG_ERROR(
				"failed to allocate memory for module data: "
					"%d\n", err);
		goto err_exit_alloc_data_failed;
	}

	mutex_init(&prs->lock);
	mutex_lock(&prs->lock);

	prs->client = client;
	i2c_set_clientdata(client, prs);

	if (i2c_smbus_read_byte(client) < 0) {
		SENSOR_LOG_ERROR("%s:i2c_smbus_read_byte error!!\n",
							LPS331AP_PRS_DEV_NAME);
		goto err_mutexunlockfreedata;
	} else {
		SENSOR_LOG_INFO("%s Device detected!\n",
							LPS331AP_PRS_DEV_NAME);
	}

	/* read chip id */
	tempvalue = i2c_smbus_read_word_data(client, WHO_AM_I);
	if ((tempvalue & 0x00FF) == WHOAMI_LPS331AP_PRS) {
		SENSOR_LOG_INFO("%s I2C driver registered!tempvalue=%d\n", LPS331AP_PRS_DEV_NAME,tempvalue);
	} else {
		prs->client = NULL;
		err = -ENODEV;
		SENSOR_LOG_ERROR("I2C driver not registered."
				" Device unknown: err=%d tempvalue = %d\n", err,tempvalue);
		goto err_mutexunlockfreedata;
	}


	prs->pdata = kmemdup(client->dev.platform_data,
					sizeof(*prs->pdata), GFP_KERNEL);
	if (!prs->pdata) {
		err = -ENOMEM;
		SENSOR_LOG_ERROR(
				"failed to allocate memory for pdata: %d\n",
				err);
		goto err_mutexunlockfreedata;
	}


	err = lps331ap_prs_validate_pdata(prs);
	if (err < 0) {
		SENSOR_LOG_ERROR("failed to validate platform data\n");
		goto err_exit_kfree_pdata;
	}


	if (prs->pdata->init) {
		err = prs->pdata->init();
		if (err < 0) {
			SENSOR_LOG_ERROR("init failed: %d\n", err);
			goto err_exit_pointer;
		}
	}

    err = prs->pdata->gpio_init();
    if (err)
    {
       SENSOR_LOG_ERROR("sensor_press_lps331ap_goio_init failed!\n");
       goto err_mutexunlockfreedata;
    }
    else
    {
       SENSOR_LOG_ERROR("sensor_press_lps331ap_goio_init success!\n");
    }

	memset(prs->resume_state, 0, ARRAY_SIZE(prs->resume_state));
	/* init registers which need values different from zero */
	prs->resume_state[LPS331AP_RES_CTRL_REG1] =
		(
			(LPS331AP_PRS_ENABLE_MASK & LPS331AP_PRS_PM_NORMAL) |
			(LPS331AP_PRS_ODR_MASK & LPS331AP_PRS_ODR_1_1) |
			(LPS331AP_PRS_BDU_MASK & LPS331AP_PRS_BDU_ON) |
			(LPS331AP_PRS_DELTA_EN_MASK & LPS331AP_PRS_DELTA_EN_ON)
		);

	prs->resume_state[LPS331AP_RES_TP_RESOL] = LPS331AP_PRS_RES_MAX;
/*
	prs->resume_state[LPS331AP_RES_CTRL_REG1] =
				(LPS331AP_PRS_PM_NORMAL | LPS331AP_PRS_ODR_1_1 |
					LPS331AP_PRS_BDU_ON));
	prs->resume_state[LPS331AP_RES_CTRL_REG2] = 0x00;
	prs->resume_state[LPS331AP_RES_CTRL_REG3] = 0x00;
	prs->resume_state[LPS331AP_RES_REF_P_L] = 0x00;
	prs->resume_state[LPS331AP_RES_REF_P_H] = 0x00;
	prs->resume_state[LPS331AP_RES_THS_P_L] = 0x00;
	prs->resume_state[LPS331AP_RES_THS_P_H] = 0x00;
	prs->resume_state[LPS331AP_RES_INT_CFG] = 0x00;
*/

	err = lps331ap_prs_device_power_on(prs);
	if (err < 0) {
		dev_err(&client->dev, "power on failed: %d\n", err);
		goto err_exit_pointer;
	}

	atomic_set(&prs->enabled, 1);

	err = lps331ap_prs_update_odr(prs, prs->pdata->poll_interval);
	if (err < 0) {
		SENSOR_LOG_ERROR("update_odr failed\n");
		goto err_power_off;
	}


	err = lps331ap_prs_input_init(prs);
	if (err < 0) {
		SENSOR_LOG_ERROR("input init failed\n");
		goto err_power_off;
	}

	err = lps331ap_prs_acquire_compensation_data(prs);
	if (err < 0) {
		SENSOR_LOG_ERROR("compensation data acquisition failed\n");
		goto err_power_off;
	}

	err = create_sysfs_interfaces(&client->dev);
	if (err < 0) {
		SENSOR_LOG_ERROR(
			"device LPS331AP_PRS_DEV_NAME sysfs register failed\n");
		goto err_input_cleanup;
	}


	lps331ap_prs_device_power_off(prs);

	/* As default, do not report information */
	atomic_set(&prs->enabled, 0);


	mutex_unlock(&prs->lock);

	SENSOR_LOG_INFO("%s: probed\n", LPS331AP_PRS_DEV_NAME);

	return 0;

/*
err_remove_sysfs_int:
	lps331ap_prs_remove_sysfs_interfaces(&client->dev);
*/
err_input_cleanup:
	lps331ap_prs_input_cleanup(prs);
err_power_off:
	lps331ap_prs_device_power_off(prs);
err_exit_pointer:
	if (prs->pdata->exit)
		prs->pdata->exit();
err_exit_kfree_pdata:
	kfree(prs->pdata);

err_mutexunlockfreedata:
	mutex_unlock(&prs->lock);
	kfree(prs);
err_exit_alloc_data_failed:
err_exit_check_functionality_failed:
	SENSOR_LOG_ERROR("%s: Driver Init failed\n", LPS331AP_PRS_DEV_NAME);
	return err;
}

static int __devexit lps331ap_prs_remove(struct i2c_client *client)
{
	struct lps331ap_prs_data *prs = i2c_get_clientdata(client);

	lps331ap_prs_input_cleanup(prs);
	lps331ap_prs_device_power_off(prs);
	remove_sysfs_interfaces(&client->dev);

	if (prs->pdata->exit)
		prs->pdata->exit();
	kfree(prs->pdata);
	kfree(prs);

	return 0;
}

#ifdef CONFIG_PM

static int lps331ap_prs_resume(struct i2c_client *client)
{
	struct lps331ap_prs_data *prs = i2c_get_clientdata(client);

	if (prs->on_before_suspend)
		return lps331ap_prs_enable(prs);
	return 0;
}

static int lps331ap_prs_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct lps331ap_prs_data *prs = i2c_get_clientdata(client);

	prs->on_before_suspend = atomic_read(&prs->enabled);
	return lps331ap_prs_disable(prs);
}

#else

#define lps001wp_prs_resume	NULL
#define	lps001wp_prs_suspend	NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id lps331ap_prs_id[]
		= { { LPS331AP_PRS_DEV_NAME, 0}, { },};

MODULE_DEVICE_TABLE(i2c, lps331ap_prs_id);

static struct i2c_driver lps331ap_prs_driver = {
	.driver = {
			.name = LPS331AP_PRS_DEV_NAME,
			.owner = THIS_MODULE,
	},
	.probe = lps331ap_prs_probe,
	.remove = __devexit_p(lps331ap_prs_remove),
	.id_table = lps331ap_prs_id,
	.resume = lps331ap_prs_resume,
	.suspend = lps331ap_prs_suspend,
};

static int __init lps331ap_prs_init(void)
{
#if DEBUG
	SENSOR_LOG_INFO("%s barometer driver: init\n", LPS331AP_PRS_DEV_NAME);
#endif
	return i2c_add_driver(&lps331ap_prs_driver);
}

static void __exit lps331ap_prs_exit(void)
{
#if DEBUG
	SENSOR_LOG_INFO("%s barometer driver exit\n", LPS331AP_PRS_DEV_NAME);
#endif
	i2c_del_driver(&lps331ap_prs_driver);
	return;
}

module_init(lps331ap_prs_init);
module_exit(lps331ap_prs_exit);

MODULE_DESCRIPTION("STMicrolelectronics lps331ap pressure sensor sysfs driver");
MODULE_AUTHOR("Matteo Dameno, Carmine Iascone, STMicroelectronics");
MODULE_LICENSE("GPL");


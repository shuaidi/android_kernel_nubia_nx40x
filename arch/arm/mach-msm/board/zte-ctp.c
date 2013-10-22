/******************************************************************************

  Copyright (C), 2001-2012, ZTE. Co., Ltd.

 ******************************************************************************
  File Name     : V5-ctp.c
  Version       : Initial Draft
  Author        : LiuYongfeng
  Created       : 2012/10/6
  Last Modified :
  Description   : This is touchscreen driver board file
  Function List :
  History       :
  1.Date        : 2012/10/6
    Author      : LiuYongfeng
    Modification: Created file

*****************************************************************************/

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/bootmem.h>
#include <linux/ion.h>
#include <asm/mach-types.h>
#include <mach/msm_memtypes.h>
#include <mach/board.h>
#include <mach/gpiomux.h>
#include <mach/ion.h>
#include <mach/msm_bus_board.h>
#include <mach/socinfo.h>

#include <linux/i2c.h>

#include "../devices.h"
#include "../board-8064.h"

#ifdef CONFIG_ZTE_TOUCH_CAPACITANCE
#include <linux/input/zte_cap_touch.h>

#define CTP_GPIO_INT 		(6)
#define CTP_GPIO_RESET  	(7)
#define CTP_GPIO_SDA  		(8)
#define CTP_GPIO_SCL  		(9)

struct regulator_bulk_data regs_ctp[] = {
	{.supply = "ctp_l17", .min_uV = 2850000, .max_uV = 2850000},
};

int zte_ctp_power(struct i2c_client *client, bool on)
{
	int rc = -1;
	struct device *dev = &client->dev;
	
	printk("%s begin\n",__func__);

	if (on) {
		rc = regulator_bulk_get(dev, ARRAY_SIZE(regs_ctp), regs_ctp);
		if (rc) {
			dev_err(dev, "%s: could not get regulators regs_l17: %d\n",__func__,rc);
			goto out;
		}
		rc = regulator_bulk_set_voltage(ARRAY_SIZE(regs_ctp), regs_ctp);
			if (rc) {
				dev_err(dev, "%s: could not get regulators regs_l17: %d\n",__func__,rc);
				goto out;
			}
		regulator_bulk_enable(ARRAY_SIZE(regs_ctp),regs_ctp);
	}else {
		regulator_bulk_disable(ARRAY_SIZE(regs_ctp),regs_ctp);
		regulator_bulk_free(ARRAY_SIZE(regs_ctp),regs_ctp);
	}
	printk("%s %s sucess\n",__func__ ,(on==true ? "on":"off"));
	return 0;
	
out:
	printk("%s %s failed\n",__func__ ,(on==true ? "on":"off"));

	return rc;
}

static int zte_ctp_hw_init(void)
{
	gpio_tlmm_config(GPIO_CFG(CTP_GPIO_RESET, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),GPIO_CFG_ENABLE);

	if(gpio_request(CTP_GPIO_RESET, "CTP_GPIO_RESET")) {
		pr_err("failed request CTP_GPIO_RESET.\n");
		return -1;
	}
	gpio_set_value(CTP_GPIO_RESET,1);
    
	gpio_tlmm_config(GPIO_CFG(CTP_GPIO_INT, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_10MA),GPIO_CFG_ENABLE);
	if(gpio_request(CTP_GPIO_INT, "CTP_GPIO_INT")) {
		pr_err("failed request CTP_GPIO_INT.\n");
		goto err_rst_gpio_req;
	}
	
	return 0;
	
err_rst_gpio_req:
	gpio_free(CTP_GPIO_RESET);
	return -1;
}

static int zte_ctp_hw_exit(void)
{
	gpio_tlmm_config(GPIO_CFG(CTP_GPIO_RESET, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
	gpio_tlmm_config(GPIO_CFG(CTP_GPIO_INT, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
	gpio_free(CTP_GPIO_RESET);
	gpio_free(CTP_GPIO_INT);
	regulator_bulk_disable(ARRAY_SIZE(regs_ctp),regs_ctp);
	regulator_bulk_free(ARRAY_SIZE(regs_ctp),regs_ctp);
	return 0;
}

static struct ts_i2c_platform_data zte_ctp_pdata = {
	.id = 0, 
	.zte_tp_parameter = {
		.dis_min_x = 0,
		.dis_max_x = 480,
		.dis_min_y = 0,
		.dis_max_y = 800,
		.min_tid   = 0,
		.max_tid   = 255,
		.min_touch = 0,
		.max_touch = 255,
		.min_width = 0,
		.max_width = 255,
		.nfingers  = 5,
	},
	.zte_tp_interface = {
		.sda = CTP_GPIO_SDA,
		.scl = CTP_GPIO_SCL,
		.irq = CTP_GPIO_INT,        
		.cs  = CTP_GPIO_RESET, 
		.reset = CTP_GPIO_RESET, 
	},
	.nvitualkeys = FourVirtualKeys,
	.zte_virtual_key[0] = {
		.keycode = KEY_BACK,
		.x = 44,
		.y = 860,
		.x_width = 120,
		.y_width = 100
	},
	.zte_virtual_key[1] = {
		.keycode = KEY_MENU,
		.x = 435,
		.y = 860,
		.x_width = 120,
		.y_width = 100
	},
	.init_tp_hw = zte_ctp_hw_init,
	.exit_tp_hw = zte_ctp_hw_exit,
	.power_on = zte_ctp_power,
};

static struct i2c_board_info zte_ctp_i2c_info[] __initdata = {
	{
		I2C_BOARD_INFO(ZTE_CTP_NAME, 0x4b),
		.platform_data = &zte_ctp_pdata,
	},
};
#endif


#ifdef CONFIG_CYPRSS_TOUCHSCREEN_TMA463
/* cyttsp */
#include <linux/cyttsp4_bus.h>
#include <linux/cyttsp4_core.h>
#include <linux/cyttsp4_btn.h>
#include <linux/cyttsp4_mt.h>
#include <linux/cyttsp4_regs.h>
#define CYTTSP4_USE_I2C

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_INCLUDE_FW
#include "cyttsp4_img.h"
static struct cyttsp4_touch_firmware cyttsp4_firmware = {
	.img = cyttsp4_img,
	.size = ARRAY_SIZE(cyttsp4_img),
	.ver = cyttsp4_ver,
	.vsize = ARRAY_SIZE(cyttsp4_ver),
};
#else
static struct cyttsp4_touch_firmware cyttsp4_firmware = {
	.img = NULL,
	.size = 0,
	.ver = NULL,
	.vsize = 0,
};
#endif

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP4_AUTO_LOAD_TOUCH_PARAMS
static struct touch_settings cyttsp4_sett_param_regs = {
	.data = (uint8_t *)&cyttsp4_param_regs[0],
	.size = ARRAY_SIZE(cyttsp4_param_regs),
	.tag = 0,
};

static struct touch_settings cyttsp4_sett_param_size = {
	.data = (uint8_t *)&cyttsp4_param_size[0],
	.size = ARRAY_SIZE(cyttsp4_param_size),
	.tag = 0,
};
#else
static struct touch_settings cyttsp4_sett_param_regs = {
	.data = NULL,
	.size = 0,
	.tag = 0,
};

static struct touch_settings cyttsp4_sett_param_size = {
	.data = NULL,
	.size = 0,
	.tag = 0,
};
#endif

static struct cyttsp4_loader_platform_data _cyttsp4_loader_platform_data = {
	.fw = &cyttsp4_firmware,
	.param_regs = &cyttsp4_sett_param_regs,
	.param_size = &cyttsp4_sett_param_size,
	.flags = 0,
};

#ifdef CYTTSP4_USE_I2C
#define CYTTSP4_I2C_NAME "cyttsp4_i2c_adapter"
#define CYTTSP4_I2C_TCH_ADR 0x24
#define CYTTSP4_LDR_TCH_ADR 0x24
#define CYTTSP4_I2C_IRQ_GPIO 6 /* J6.9, C19, GPMC_AD14/GPIO_38 */
#define CYTTSP4_I2C_RST_GPIO 7 /* J6.10, D18, GPMC_AD13/GPIO_37 */
#endif

#ifdef CYTTSP4_USE_SPI
#define CYTTSP4_SPI_NAME "cyttsp4_spi_adapter"
/* Change GPIO numbers when using I2C and SPI at the same time
 * Following is possible alternative:
 * IRQ: J6.17, C18, GPMC_AD12/GPIO_36
 * RST: J6.24, D17, GPMC_AD11/GPIO_35
 */
#define CYTTSP4_SPI_IRQ_GPIO 38 /* J6.9, C19, GPMC_AD14/GPIO_38 */
#define CYTTSP4_SPI_RST_GPIO 37 /* J6.10, D18, GPMC_AD13/GPIO_37 */
#endif

/* Check GPIO numbers if both I2C and SPI are enabled */
#if defined(CYTTSP4_USE_I2C) && defined(CYTTSP4_USE_SPI)
#if CYTTSP4_I2C_IRQ_GPIO == CYTTSP4_SPI_IRQ_GPIO || \
	CYTTSP4_I2C_RST_GPIO == CYTTSP4_SPI_RST_GPIO
#error "GPIO numbers should be different when both I2C and SPI are on!"
#endif
#endif

#ifdef CONFIG_CYTTSP4_5_INCH_OTG_GW //goworld 5 inch otg tp for NX501/NX502
#define CY_MAXX 1080
#define CY_MAXY 1920
#endif

#ifdef CONFIG_CYTTSP4_4D7_INCH_OTG_GW
#define CY_MAXX 720
#define CY_MAXY 1280
#endif
#define CY_MINX 0
#define CY_MINY 0

#define CY_ABS_MIN_X CY_MINX
#define CY_ABS_MIN_Y CY_MINY
#define CY_ABS_MAX_X CY_MAXX
#define CY_ABS_MAX_Y CY_MAXY
#define CY_ABS_MIN_P 0
#define CY_ABS_MAX_P 255
#define CY_ABS_MIN_W 0
#define CY_ABS_MAX_W 255

#define CY_ABS_MIN_T 0

#define CY_ABS_MAX_T 15

#define CY_IGNORE_VALUE 0xFFFF

//#define CYTTSP4_VIRTUAL_KEYS

static int cyttsp4_xres(struct cyttsp4_core_platform_data *pdata,
		struct device *dev)
{
	int rst_gpio = pdata->rst_gpio;
	int rc = 0;
	
	gpio_set_value(rst_gpio, 1);
	msleep(20);
	gpio_set_value(rst_gpio, 0);
	msleep(40);
	gpio_set_value(rst_gpio, 1);
	msleep(20);

	return rc;
}


static int cypress_power_on(struct device *dev)
{
	static struct regulator *reg_l17;
	int rc;

	reg_l17 = regulator_get(dev,
							"8921_l17");
	if (IS_ERR(reg_l17)) {
			pr_err("could not get reg_l17, rc = %ld\n",
					PTR_ERR(reg_l17));
			return -ENODEV;
	}
	rc = regulator_set_voltage(reg_l17, 2850000, 2850000);
	if (rc) {
			pr_err("set_voltage reg_l17 failed, rc=%d\n", rc);
			return -EINVAL;
	}
	rc = regulator_enable(reg_l17);
	if (rc) {
		pr_err("enable reg_l17 failed, rc=%d\n", rc);
		return -ENODEV;
	}
	return 0;
}

static bool  cyttsp4_power_state = false;
static int cyttsp4_init(struct cyttsp4_core_platform_data *pdata,
		int on, struct device *dev)
{
	int rst_gpio = pdata->rst_gpio;
	int irq_gpio = pdata->irq_gpio;
	int rc = 0;

	if (!pdata) {
		printk("%s NULL Pointer detected!\n",__func__);
		WARN_ON(1);
		return -EINVAL;
	}
	if (!cyttsp4_power_state) {
		rc = cypress_power_on(dev);
		if (rc < 0) {
			dev_err(dev,
				"%s: Fail to power on cyttsp4 %d\n", __func__,
				rc);
			return rc;
		}
		cyttsp4_power_state = true;
	}
	
	if (on) {
        rc = gpio_request(rst_gpio, "CYTTSP4_I2C_RST_GPIO");
		if (rc < 0) {
			pr_err("failed request CYTTSP4_I2C_RST_GPIO.\n");
			return rc;
		}
		gpio_tlmm_config(GPIO_CFG(rst_gpio, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
		gpio_set_value(rst_gpio,1);
        
		rc = gpio_request(irq_gpio, "CYTTSP4_I2C_IRQ_GPIO");
		if (rc < 0) {
			pr_err("failed request CYTTSP4_I2C_IRQ_GPIO.\n");
			return rc;
		}
		gpio_tlmm_config(GPIO_CFG(irq_gpio, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_10MA),GPIO_CFG_ENABLE);
		
	}else {
		gpio_free(rst_gpio);
		gpio_free(irq_gpio);
	}
	return rc;
}

static int cyttsp4_wakeup(struct cyttsp4_core_platform_data *pdata,
		struct device *dev, atomic_t *ignore_irq)
{
	int irq_gpio = pdata->irq_gpio;
	int rc = 0;

	if (ignore_irq)
		atomic_set(ignore_irq, 1);
	rc = gpio_direction_output(irq_gpio, 0);
	if (rc < 0) {
		if (ignore_irq)
			atomic_set(ignore_irq, 0);
		dev_err(dev,
			"%s: Fail set output gpio=%d\n",
			__func__, irq_gpio);
		goto wakeup_failed;
	} else {
		msleep(2);
		rc = gpio_direction_input(irq_gpio);
		if (ignore_irq)
			atomic_set(ignore_irq, 0);
		if (rc < 0) {
			dev_err(dev,
				"%s: Fail set input gpio=%d\n",
				__func__, irq_gpio);
			goto wakeup_failed;
		}
	}
	return rc;
wakeup_failed:
	dev_err(dev,
	"%s: WAKEUP CYTTSP failed, gpio=%d r=%d\n", __func__,
	irq_gpio, rc);
	return rc;
}

static int cyttsp4_sleep(struct cyttsp4_core_platform_data *pdata,
		struct device *dev, atomic_t *ignore_irq)
{
	return 0;
}

static int cyttsp4_power(struct cyttsp4_core_platform_data *pdata,
		int on, struct device *dev, atomic_t *ignore_irq)
{
	if (on)
		return cyttsp4_wakeup(pdata, dev, ignore_irq);

	return cyttsp4_sleep(pdata, dev, ignore_irq);
}

static int cyttsp4_irq_stat(struct cyttsp4_core_platform_data *pdata,
		struct device *dev)
{
	return gpio_get_value(pdata->irq_gpio);
}

static int cyttsp_check_version(struct cyttsp4_sysinfo *si)
{
    u16 fw_ver;
    u16 fw_ver_new;
    
	if (!si) {
		printk("%s NULL Pointer detected!\n",__func__);
		WARN_ON(1);
		return -EINVAL;
	}

    si->fw_ver_new = 0x0000;

    fw_ver = si->fw_ver & 0x00FF; 
    fw_ver_new = si->fw_ver_new & 0x00FF;

	printk("%s: TP FW version:0x%04X new FW version:0x%04X\n", __func__,
		si->fw_ver, si->fw_ver_new);
#if 0
	/*Check Resolution*/
    if ((si->x_res != CY_MAXX) || (si->y_res != CY_MAXY)) {
        printk("%s: Resolution didn't match,no fw upgarde!\n", __func__);
        return -1;
    }
#endif
	if ((si->fw_ver >> 8) == (si->fw_ver_new >> 8)) {
        if (fw_ver_new  == fw_ver) {
        	/*equal*/
        	return 0;
    	}
        
    	if (fw_ver_new > fw_ver) {
    		printk("%s: Image is newer, will upgrade\n", __func__);
    		return 1;
    	}

    	printk("%s: Image is older, would not upgrade\n", __func__);
    	return -1;
	}
    else
    {
		printk("%s: FW didn't match, would not upgarde!\n", __func__);
		return -1;
    }
}


/* Button to keycode conversion */
static u16 cyttsp4_btn_keys[] = {
	/* use this table to map buttons to keycodes (see input.h) */
	KEY_MENU, 		/*139*/
	KEY_HOME,		/*102*/
	KEY_BACK,		/*158*/
//	KEY_HOME,		/* 102 */
//	KEY_MENU,		/* 139 */
//	KEY_BACK,		/* 158 */
//	KEY_SEARCH,		/* 217 */
	KEY_VOLUMEDOWN,		/* 114 */
	KEY_VOLUMEUP,		/* 115 */
	KEY_CAMERA,		/* 212 */
	KEY_POWER		/* 116 */
};

static struct touch_settings cyttsp4_sett_btn_keys = {
	.data = (uint8_t *)&cyttsp4_btn_keys[0],
	.size = ARRAY_SIZE(cyttsp4_btn_keys),
	.tag = 0,
};

static struct cyttsp4_core_platform_data _cyttsp4_core_platform_data = {
	.irq_gpio = CYTTSP4_I2C_IRQ_GPIO,
	.rst_gpio = CYTTSP4_I2C_RST_GPIO,
	.xres = cyttsp4_xres,
	.init = cyttsp4_init,
	.power = cyttsp4_power,
	.irq_stat = cyttsp4_irq_stat,
	.check_version = cyttsp_check_version,
	.sett = {
		NULL,	/* Reserved */
		NULL,	/* Command Registers */
		NULL,	/* Touch Report */
		NULL,	/* Cypress Data Record */
		NULL,	/* Test Record */
		NULL,	/* Panel Configuration Record */
		NULL, /* &cyttsp4_sett_param_regs, */
		NULL, /* &cyttsp4_sett_param_size, */
		NULL,	/* Reserved */
		NULL,	/* Reserved */
		NULL,	/* Operational Configuration Record */
		NULL, /* &cyttsp4_sett_ddata, *//* Design Data Record */
		NULL, /* &cyttsp4_sett_mdata, *//* Manufacturing Data Record */
		NULL,	/* Config and Test Registers */
		&cyttsp4_sett_btn_keys,	/* button-to-keycode table */
	},
	.loader_pdata = &_cyttsp4_loader_platform_data,
};

static struct cyttsp4_core_info cyttsp4_core_info __initdata = {
	.name = CYTTSP4_CORE_NAME,
	.id = "main_ttsp_core",
	.adap_id = CYTTSP4_I2C_NAME,
	.platform_data = &_cyttsp4_core_platform_data,
};


static const uint16_t cyttsp4_abs[] = {
	ABS_MT_POSITION_X, CY_ABS_MIN_X, CY_ABS_MAX_X, 0, 0,
	ABS_MT_POSITION_Y, CY_ABS_MIN_Y, CY_ABS_MAX_Y, 0, 0,
	ABS_MT_PRESSURE, CY_ABS_MIN_P, CY_ABS_MAX_P, 0, 0,
	CY_IGNORE_VALUE, CY_ABS_MIN_W, CY_ABS_MAX_W, 0, 0,
	ABS_MT_TRACKING_ID, CY_ABS_MIN_T, CY_ABS_MAX_T, 0, 0,
	ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0,
	ABS_MT_TOUCH_MINOR, 0, 255, 0, 0,
	ABS_MT_ORIENTATION, -128, 127, 0, 0,
};

struct touch_framework cyttsp4_framework = {
	.abs = (uint16_t *)&cyttsp4_abs[0],
	.size = ARRAY_SIZE(cyttsp4_abs),
	.enable_vkeys = 0,
};

static struct cyttsp4_mt_platform_data _cyttsp4_mt_platform_data = {
	.frmwrk = &cyttsp4_framework,
	.flags = 0x00,//0x38,yfliu
	.inp_dev_name = CYTTSP4_MT_NAME,
};

struct cyttsp4_device_info cyttsp4_mt_info __initdata = {
	.name = CYTTSP4_MT_NAME,
	.core_id = "main_ttsp_core",
	.platform_data = &_cyttsp4_mt_platform_data,
};


static struct cyttsp4_btn_platform_data _cyttsp4_btn_platform_data = {
	.inp_dev_name = CYTTSP4_BTN_NAME,
};

struct cyttsp4_device_info cyttsp4_btn_info __initdata = {
	.name = CYTTSP4_BTN_NAME,
	.core_id = "main_ttsp_core",
	.platform_data = &_cyttsp4_btn_platform_data,
};


#ifdef CYTTSP4_VIRTUAL_KEYS
static ssize_t cyttps4_virtualkeys_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
		__stringify(EV_KEY) ":"
		__stringify(KEY_BACK) ":1360:90:160:180"
		":" __stringify(EV_KEY) ":"
		__stringify(KEY_MENU) ":1360:270:160:180"
		":" __stringify(EV_KEY) ":"
		__stringify(KEY_HOME) ":1360:450:160:180"
		":" __stringify(EV_KEY) ":"
		__stringify(KEY_SEARCH) ":1360:630:160:180"
		"\n");
}

static struct kobj_attribute cyttsp4_virtualkeys_attr = {
	.attr = {
		.name = "virtualkeys.cyttsp4_mt",
		.mode = S_IRUGO,
	},
	.show = &cyttps4_virtualkeys_show,
};

static struct attribute *cyttsp4_properties_attrs[] = {
	&cyttsp4_virtualkeys_attr.attr,
	NULL
};

static struct attribute_group cyttsp4_properties_attr_group = {
	.attrs = cyttsp4_properties_attrs,
};
#endif
static void __init cyttsp4_i2c_device_init(void)
{
#ifdef CYTTSP4_VIRTUAL_KEYS
	struct kobject *properties_kobj;
	int ret = 0;
#endif
	/* Register core and devices */
	cyttsp4_register_core_device(&cyttsp4_core_info);
	cyttsp4_register_device(&cyttsp4_mt_info);
	cyttsp4_register_device(&cyttsp4_btn_info);

#ifdef CYTTSP4_VIRTUAL_KEYS
	properties_kobj = kobject_create_and_add("board_properties", NULL);
	if (properties_kobj)
		ret = sysfs_create_group(properties_kobj,
				&cyttsp4_properties_attr_group);
	if (!properties_kobj || ret)
		pr_err("%s: failed to create board_properties\n", __func__);
#endif
}

static struct i2c_board_info cypress_tma463_ts_i2c_info[] __initdata= {
	{
		I2C_BOARD_INFO(CYTTSP4_I2C_NAME, CYTTSP4_I2C_TCH_ADR),
		.irq = MSM_GPIO_TO_INT(CYTTSP4_I2C_IRQ_GPIO),
		.platform_data = CYTTSP4_I2C_NAME,
	},
};

#endif

static int  __init zte_init_ctp(void)
{
	int rc = 0;

#ifdef CONFIG_ZTE_TOUCH_CAPACITANCE
	rc = i2c_register_board_info(APQ_8064_GSBI3_QUP_I2C_BUS_ID,
								  zte_ctp_i2c_info, 
								  ARRAY_SIZE(zte_ctp_i2c_info));
#endif

#ifdef CONFIG_CYPRSS_TOUCHSCREEN_TMA463
	cyttsp4_i2c_device_init();

	rc = i2c_register_board_info(APQ_8064_GSBI3_QUP_I2C_BUS_ID,
								  cypress_tma463_ts_i2c_info, 
								  ARRAY_SIZE(cypress_tma463_ts_i2c_info));
#endif
	return rc;
}

arch_initcall(zte_init_ctp);


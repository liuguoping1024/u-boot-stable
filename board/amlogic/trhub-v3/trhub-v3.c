// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Vyacheslav Bocharov
 * Author: Vyacheslav Bocharov <adeep@lexina.in>
 */

#include <common.h>
#include <command.h>
#include <dm.h>
#include <init.h>
#include <net.h>
#include <asm/io.h>
#include <asm/arch/axg.h>
#include <asm/arch/sm.h>
#include <asm/arch/eth.h>
#include <asm/arch/mem.h>
#include <u-boot/crc.h>

#include <asm/gpio.h>

/*
 * GPIO Bank 顺序在不同 U-Boot 版本中发生了变化:
 * 
 * v2022.10: aobus-banks(0-13), periphs-banks(14-99)
 * v2023.10: periphs-banks(0-85), aobus-banks(86-99)
 * 
 * 硬件连接:
 * - RGB_RED:    GPIOAO_3  
 * - RGB_GREEN:  GPIOZ_4   
 * - RGB_BLUE:   GPIOZ_5   
 * - ZIGBEE_RXD: GPIOZ_9   
 * - THREAD_RXD: GPIOX_17  
 * 
 * 使用 UBOOT_GPIO_BANK_V2023 宏来选择版本:
 * - 不定义(默认): 使用 v2022.10 的 GPIO 编号
 * - 定义为 1:      使用 v2023.10 的 GPIO 编号
 */

#if 1
/* U-Boot v2023.10+: periphs-banks(0-85), aobus-banks(86-99) */
#define RGB_RED_GPIO		3  //GPIOAO_3 0 + 3 = 3
#define RGB_GREEN_GPIO		18  //GPIOZ_4 14 + 4 = 18
#define RGB_BLUE_GPIO		19  //GPIOZ_5 14 + 5 = 19
#define ZIGBEE_RXD_GPIO		23 //GPIOZ_9 14 + 9 = 23	
#define THREAD_RXD_GPIO		78 //GPIOX_17 47 + 17 = 64
#else
/* U-Boot v2022.10: aobus-banks(0-13), periphs-banks(14-99) */
#define RGB_RED_GPIO		89  //GPIOAO_3 86 + 3 = 89
#define RGB_GREEN_GPIO		4  //GPIOZ_4 14 + 4 = 18
#define RGB_BLUE_GPIO		5  //GPIOZ_5 14 + 5 = 19	
#define ZIGBEE_RXD_GPIO		9 //GPIOZ_9 14 + 9 = 23
#define THREAD_RXD_GPIO		64 //GPIOX_17 47 + 17 = 64

#endif



/* RGB LED 初始化函数 */
static int rgb_led_init(void)
{
	int ret;

	/* 初始化红色 LED GPIO - 直接设置为高电平 */
	ret = gpio_request(RGB_RED_GPIO, "rgb_red");
	if (ret) {
		printf("RGB LED: Failed to request red GPIO %d\n", RGB_RED_GPIO);
		return ret;
	}
	gpio_direction_output(RGB_RED_GPIO, 1);

	/* 初始化绿色 LED GPIO - 直接设置为高电平 */
	ret = gpio_request(RGB_GREEN_GPIO, "rgb_green");
	if (ret) {
		printf("RGB LED: Failed to request green GPIO %d\n", RGB_GREEN_GPIO);
		goto err_green;
	}
	gpio_direction_output(RGB_GREEN_GPIO, 1);

	/* 初始化蓝色 LED GPIO - 直接设置为高电平 */
	ret = gpio_request(RGB_BLUE_GPIO, "rgb_blue");
	if (ret) {
		printf("RGB LED: Failed to request blue GPIO %d\n", RGB_BLUE_GPIO);
		goto err_blue;
	}
	gpio_direction_output(RGB_BLUE_GPIO, 1);

	printf("RGB LED initialized successfully (all pins set to HIGH)\n");
	return 0;

err_blue:
	gpio_free(RGB_GREEN_GPIO);
err_green:
	gpio_free(RGB_RED_GPIO);
	return ret;
}

/* GPIO 输入初始化函数 */
static int gpio_input_init(void)
{
	int ret;

	/* 初始化 ZIGBEE_RXD_GPIO - 设置为输入模式 */
	ret = gpio_request(ZIGBEE_RXD_GPIO, "zigbee_rxd");
	if (ret) {
		printf("GPIO INPUT: Failed to request ZIGBEE_RXD GPIO %d\n", ZIGBEE_RXD_GPIO);
		return ret;
	}
	gpio_direction_input(ZIGBEE_RXD_GPIO);

	/* 初始化 THREAD_RXD_GPIO - 设置为输入模式 */
	ret = gpio_request(THREAD_RXD_GPIO, "thread_rxd");
	if (ret) {
		printf("GPIO INPUT: Failed to request THREAD_RXD GPIO %d\n", THREAD_RXD_GPIO);
		goto err_thread;
	}
	gpio_direction_input(THREAD_RXD_GPIO);

	printf("GPIO inputs initialized successfully (ZIGBEE_RXD: %d, THREAD_RXD: %d)\n", 
		   ZIGBEE_RXD_GPIO, THREAD_RXD_GPIO);
	return 0;

err_thread:
	gpio_free(ZIGBEE_RXD_GPIO);
	return ret;
}


int misc_init_r(void)
{
	u8 mac_addr[ARP_HLEN];
	char serial[SM_SERIAL_SIZE];
	u32 sid;
	int ret;

	char _cmdbuf[96];
	char keyname[32];
	char keydata[256];
	int ver=0;

	/* 初始化 RGB LED */
	ret = rgb_led_init();
	if (ret) {
		printf("Warning: RGB LED initialization failed\n");
	} else {
		/* RGB LED 初始化时已经设置为白色（所有引脚为高电平） */
		printf("RGB LED initialized as WHITE (all pins HIGH)\n");
	}

	/* 初始化 GPIO 输入引脚 */
	ret = gpio_input_init();
	if (ret) {
		printf("Warning: GPIO input initialization failed\n");
	} else {
		printf("GPIO inputs initialized successfully\n");
	}

	memset (mac_addr,0, sizeof(mac_addr));
	sprintf(_cmdbuf, "store init");
	if(!run_command(_cmdbuf, 0))
	{
		sprintf(_cmdbuf, "keyman init 0x1234");
		if(!run_command(_cmdbuf, 0))
		{
			strcpy(keyname, "usid");
			memset (keydata, 0, sizeof(keydata));
			sprintf(_cmdbuf, "keyman read %s %p str", keyname, keydata);
			ret = run_command(_cmdbuf, 0);
			if (!ret)
			{
#if 0				
			// j100__04012201sw00016142005c
			// 0123456789
			  if (keydata[0] == 'j')
			    {
			      if (keydata[1] == '1')
			      {
					sprintf(_cmdbuf, "%c%c",keydata[6],keydata[7]);
					env_set("hwrev", _cmdbuf);
					sprintf(_cmdbuf, "%c%c",keydata[8],keydata[9]);
					env_set("perev", _cmdbuf);
			      }
			    }
#endif			
			}

			// get serial
			strcpy(keyname, "serial");
			memset (keydata, 0, sizeof(keydata));
			sprintf(_cmdbuf, "keyman read %s %p str", keyname, keydata);
			ret = run_command(_cmdbuf, 0);

			// get mac
			strcpy(keyname, "mac");
			memset (keydata, 0, sizeof(keydata));
			sprintf(_cmdbuf, "keyman read %s %#p str", keyname, keydata);
			ret = run_command(_cmdbuf, 0);
			if (keydata[2]==':') 
			{
				keydata[17] = (char) 0x00;
				sprintf(_cmdbuf,"env set ethaddr %s", keydata);
				ret = run_command(_cmdbuf, 0);
				mac_addr[0] = (char) 0x01;
			} 
			//else 
			//{
			//	printf("keyman read mac failed\n");
			//}
		}
	}

	if (mac_addr[0]==0)
	  if (!meson_sm_get_serial(serial, SM_SERIAL_SIZE)) {
		sid = crc32(0, (unsigned char *)serial, SM_SERIAL_SIZE);
		/* Ensure the NIC specific bytes of the mac are not all 0 */
		if ((sid & 0xffff) == 0)
			sid |= 0x800000;

		/* OUI registered MAC address */
		mac_addr[0] = 0x10;
		mac_addr[1] = 0x27;
		mac_addr[2] = 0xBE;
		mac_addr[3] = (sid >> 16) & 0xff;
		mac_addr[4] = (sid >>  8) & 0xff;
		mac_addr[5] = (sid >>  0) & 0xff;
		eth_env_set_enetaddr("ethaddr", mac_addr);
	  }

	return 0;
}

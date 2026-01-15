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

int misc_init_r(void)
{
	u8 mac_addr[ARP_HLEN];
	int ret;

	char _cmdbuf[96];
	char keyname[32];
	char keydata[256];
	int ver=0;

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
			} else 
			{
				printf("keyman read mac failed\n");
			}
		}
	}

	meson_generate_serial_ethaddr();

	return 0;
}

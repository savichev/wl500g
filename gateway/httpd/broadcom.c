/*
 * Broadcom Home Gateway Reference Design
 * Web Page Configuration Support Routines
 *
 * Copyright 2004, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <net/if.h>

#include <typedefs.h>
#include <proto/ethernet.h>
#include <bcmnvram.h>
#include <bcmutils.h>
#include <shutils.h>
#include <netconf.h>
#include <nvparse.h>
#include <wlutils.h>

#define wan_prefix(unit, prefix)	snprintf(prefix, sizeof(prefix), "wan%d_", unit)


#define EZC_FLAGS_READ		0x0001
#define EZC_FLAGS_WRITE		0x0002
#define EZC_FLAGS_CRYPT		0x0004

#define EZC_CRYPT_KEY		"620A83A6960E48d1B05D49B0288A2C1F"

#define EZC_SUCCESS	 	0
#define EZC_ERR_NOT_ENABLED 	1
#define EZC_ERR_INVALID_STATE 	2
#define EZC_ERR_INVALID_DATA 	3


typedef u_int64_t u64;
typedef u_int32_t u32;
typedef u_int16_t u16;
typedef u_int8_t u8;

#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <net/if_arp.h>

#include "httpd.h"

/* Renew lease */
int
sys_renew(void)
{
	int unit;
	char tmp[100];

	if ((unit = nvram_get_int("wan_unit")) < 0)
		unit = 0;

#ifdef REMOVE	
	char *str;
	int pid;

	snprintf(tmp, sizeof(tmp), "/var/run/udhcpc%d.pid", unit);
	if ((str = file2str(tmp))) {
		pid = atoi(str);
		free(str);
		return kill(pid, SIGUSR1);
	}	
	return -1;
#else
	snprintf(tmp, sizeof(tmp), "wan_connect,%d", unit);
	nvram_set("rc_service", tmp);
	return kill(1, SIGUSR1);
#endif
}

/* Release lease */
int
sys_release(void)
{
	int unit;
	char tmp[100];

	if ((unit = nvram_get_int("wan_unit")) < 0)
		unit = 0;
	
#ifdef REMOVE
	char *str;
	int pid;

	snprintf(tmp, sizeof(tmp), "/var/run/udhcpc%d.pid", unit);
	if ((str = file2str(tmp))) {
		pid = atoi(str);
		free(str);
		return kill(pid, SIGUSR2);
	}	
	return -1;
#else	
	snprintf(tmp, sizeof(tmp), "wan_disconnect,%d", unit);
	nvram_set("rc_service", tmp);
	return kill(1, SIGUSR1);
#endif
}

static int
ej_wl_sta_status(int eid, webs_t wp, char *ifname)
{
	int ret;
	unsigned char bssid[32];

	// Get bssid
	ret = wl_ioctl(ifname, WLC_GET_BSSID, bssid, sizeof(bssid));

	if (ret == 0 && memcmp(bssid, "\x00\x00\x00\x00\x00", 6))
	{
		uint32 rssi;

		ret = websWrite(wp, "Status	: Connected to %s\n"
				    "AP	: %02x:%02x:%02x:%02x:%02x:%02x\n",
				nvram_safe_get("wl0_ssid"),
				bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

		if (wl_ioctl(ifname, WLC_GET_RSSI, &rssi, sizeof(rssi)) == 0)
			ret += websWrite(wp, "Signal	: %d dBm\n", rssi);

		return ret;
	}
	return websWrite(wp, "Status	: Connecting to %s\n", nvram_safe_get("wl0_ssid"));
}

int
ej_wl_status(int eid, webs_t wp, int argc, char **argv)
{
	int unit;
	char tmp[100], prefix[sizeof("wlXXXXXXXXXX_")];
	char *name;
	struct maclist *auth;
	int max_sta_count, maclist_size;
	int i, val;
	int ret = 0;
	channel_info_t ci;
	char chanspec[16]; /* sizeof("255 + 255") */
	sta_info_t *sta;

	if ((unit = nvram_get_int("wl_unit")) < 0)
		return -1;

	snprintf(prefix, sizeof(prefix), "wl%d_", unit);
	name = nvram_safe_get(strcat_r(prefix, "ifname", tmp));		
	wl_ioctl(name, WLC_GET_RADIO, &val, sizeof(val));
	if (val == 1) {
		ret += websWrite(wp, "Radio is disabled\n");
		return 0;
	}
	
	/* query wl for chanspec */
	strncpy(chanspec, "chanspec", sizeof(chanspec));
	if (wl_ioctl(name, WLC_GET_VAR, &chanspec, sizeof(chanspec)) == 0) {
		val = *(chanspec_t *) &chanspec;
		snprintf(chanspec, sizeof(chanspec),
		    CHSPEC_IS40(val) ? "%d + %d" : "%d",
		    CHSPEC_IS40(val) ? CHSPEC_CTL_CHAN(val)
				     : CHSPEC_CHANNEL(val),
		    CHSPEC_SB_LOWER(val) ? UPPER_20_SB(CHSPEC_CHANNEL(val))
					 : LOWER_20_SB(CHSPEC_CHANNEL(val)));
	} else {
		wl_ioctl(name, WLC_GET_CHANNEL, &ci, sizeof(ci));
		snprintf(chanspec, sizeof(chanspec), "%d", ci.target_channel);
	}

	if (nvram_match(strcat_r(prefix, "mode", tmp), "ap")) {
		if (nvram_match("wl_lazywds", "1") ||
		    nvram_match("wl_wdsapply_x", "1"))
			ret += websWrite(wp, "Mode	: Hybrid\n");
		else    ret += websWrite(wp, "Mode	: AP Only\n");
		ret += websWrite(wp, "Channel	: %s\n", chanspec);

	} else if (nvram_match(strcat_r(prefix, "mode", tmp), "wds")) {
		ret += websWrite(wp, "Mode	: WDS Only\n");
		ret += websWrite(wp, "Channel	: %s\n", chanspec);
	} else if (nvram_match(strcat_r(prefix, "mode", tmp), "sta")) {
		ret += websWrite(wp, "Mode	: Station\n");
		ret += websWrite(wp, "Channel	: %s\n", chanspec);
		ret += ej_wl_sta_status(eid, wp, name);
		return ret;
	} else if (nvram_match(strcat_r(prefix, "mode", tmp), "wet")) {
		ret += websWrite(wp, "Mode	: Ethernet Bridge\n");
		ret += websWrite(wp, "Channel	: %s\n", chanspec);
		ret += ej_wl_sta_status(eid, wp, name);
		return ret;
	}

	/* buffers and length */
	max_sta_count = 256;
	maclist_size = sizeof(auth->count) + max_sta_count * sizeof(struct ether_addr);

	auth = malloc(maclist_size);
	sta = malloc(sizeof(sta_info_t));
	if (!auth || !sta)
		goto exit;

	/* query wl for authenticated sta list */
	strcpy((char*)auth, "authe_sta_list");
	if (wl_ioctl(name, WLC_GET_VAR, auth, maclist_size))
		goto exit;

	websWrite(wp, "\n");
	websWrite(wp, "Stations List                           Mode       Joined      Idle    TX    RX\n");
	websWrite(wp, "-------------------------------------------------------------------------------\n");
	//             00:00:00:00:00:00 associated authorized 802.11n 000:00:00 000:00:00 000.0 000.0

	/* build authenticated/associated/authorized sta list */
	for (i = 0; i < auth->count; i++) {
		char ea[ETHER_ADDR_STR_LEN];

		websWrite(wp, "%s", ether_etoa((void *)&auth->ea[i], ea));

		strcpy((char*)sta, "sta_info");
		memcpy((char*)sta + sizeof("sta_info"), (void *)&auth->ea[i], ETHER_ADDR_LEN);
		if (wl_ioctl(name, WLC_GET_VAR, sta, sizeof(sta_info_t)))
			goto next;
		
		websWrite(wp, " %10s", (sta->flags & WL_STA_ASSOC) ? "associated" : "");
		websWrite(wp, " %10s", (sta->flags & WL_STA_AUTHO) ? "authorized" : "");
		websWrite(wp, " %7s", (sta->flags & WL_STA_N_CAP) ? "802.11n" : "legacy");
		websWrite(wp, " %3d:%02d:%02d",
		    sta->in / 3600, (sta->in % 3600) / 60, sta->in % 60);
		websWrite(wp, " %3d:%02d:%02d",
		    sta->idle / 3600, (sta->idle % 3600) / 60, sta->idle % 60);

		if ((sta->len >= sizeof(sta_info_t)) &&
		    (sta->flags & WL_STA_SCBSTATS)) {
			websWrite(wp, " %5.1f", sta->tx_rate/1000.0);
			websWrite(wp, " %5.1f", sta->rx_rate/1000.0);
		}
	next:
		websWrite(wp, "\n");
	}

	/* error/exit */
exit:
	if (auth) free(auth);
	if (sta) free(sta);

	return 0;
}


/* Dump NAT table <tr><td>destination</td><td>MAC</td><td>IP</td><td>expires</td></tr> format */
int
ej_nat_table(int eid, webs_t wp, int argc, char **argv)
{
	int ret = 0;
	netconf_nat_t nat_list;
	netconf_fw_t *fw;
	char line[256], tstr[32];

	ret += websWrite(wp, "Source          Destination     Proto.  Port range  Redirect to     Local port\n");
	/*                   "255.255.255.255 255.255.255.255 ALL     65535:65535 255.255.255.255 65535:65535" */

	if (netconf_get_nat(&nat_list) != 0)
		return ret;

	netconf_list_for_each(fw, (netconf_fw_t *)&nat_list) {
		const netconf_nat_t *nat = (netconf_nat_t *)fw;
		//dprintf("%d %d %d\n", nat->target,
	        //		nat->match.ipproto,
		//		nat->match.dst.ipaddr.s_addr);	
		if (nat->target == NETCONF_DNAT)	{
			/* Source */
			if (nat->match.src.ipaddr.s_addr == 0)
				sprintf(line, "%-15s", "ALL");
			else
				sprintf(line, "%-15s", inet_ntoa(nat->match.src.ipaddr));

			/* Destination */
			if (nat->match.dst.ipaddr.s_addr == 0)
				sprintf(line, "%s %-15s", line, "ALL");
			else
				sprintf(line, "%s %-15s", line, inet_ntoa(nat->match.dst.ipaddr));

                            /* Proto. */
			if (ntohs(nat->match.dst.ports[0]) == 0)
				sprintf(line, "%s %-7s", line, "ALL");
			else if (nat->match.ipproto == IPPROTO_TCP)
				sprintf(line, "%s %-7s", line, "TCP");
			else if (nat->match.ipproto == IPPROTO_UDP)
				sprintf(line, "%s %-7s", line, "UDP");
			else
				sprintf(line, "%s %-7d", line, nat->match.ipproto);

			/* Port range */
			if (nat->match.dst.ports[0] == nat->match.dst.ports[1])
			{
				if (ntohs(nat->match.dst.ports[0]) == 0)
					sprintf(line, "%s %-11s", line, "ALL");
				else
					sprintf(line, "%s %-11d", line, ntohs(nat->match.dst.ports[0]));
			} else {
				sprintf(tstr, "%d:%d",
					ntohs(nat->match.dst.ports[0]),
					ntohs(nat->match.dst.ports[1]));
				sprintf(line, "%s %-11s", line, tstr);
			}

			/* Redirect to */
			sprintf(line, "%s %-15s", line, inet_ntoa(nat->ipaddr));

			/* Local port */
			if (nat->ports[0] == nat->ports[1])
			{
				if (ntohs(nat->ports[0]) == 0)
					sprintf(line, "%s %-11s", line, "ALL");
				else
					sprintf(line, "%s %-11d", line, ntohs(nat->ports[0]));
			} else {
				sprintf(tstr, "%d:%d",
					ntohs(nat->ports[0]),
					ntohs(nat->ports[1]));
				sprintf(line, "%s %-11s", line, tstr);
			}

			sprintf(line, "%s\n", line);
			ret += websWrite(wp, line);
		}
	}
	netconf_list_free((netconf_fw_t *)&nat_list);

	return ret;
}

int
ej_route_table(int eid, webs_t wp, int argc, char **argv)
{
	char buff[256];
	int  nl = 0 ;
	struct in_addr dest;
	struct in_addr gw;
	struct in_addr mask;
	int flgs, ref, use, metric;
	int ret = 0;
	char flags[4];
	unsigned long int d,g,m;
	char sdest[16], sgw[16];
	FILE *fp;

        ret += websWrite(wp, "Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");

	if (!(fp = fopen("/proc/net/route", "r"))) return 0;

	while (fgets(buff, sizeof(buff), fp) != NULL ) 
	{
		if (nl) 
		{
			int ifl = 0;
			while (buff[ifl]!=' ' && buff[ifl]!='\t' && buff[ifl]!='\0')
				ifl++;
			buff[ifl]=0;    /* interface */
			if (sscanf(buff+ifl+1, "%lx%lx%d%d%d%d%lx",
			   &d, &g, &flgs, &ref, &use, &metric, &m)!=7) {
				//error_msg_and_die( "Unsuported kernel route format\n");
				//continue;
			}

			ifl = 0;        /* parse flags */
			if (flgs&1)
				flags[ifl++]='U';
			if (flgs&2)
				flags[ifl++]='G';
			if (flgs&4)
				flags[ifl++]='H';
			flags[ifl]=0;
			dest.s_addr = d;
			gw.s_addr   = g;
			mask.s_addr = m;
			strcpy(sdest,  (dest.s_addr==0 ? "default" :
					inet_ntoa(dest)));
			strcpy(sgw,    (gw.s_addr==0   ? "*"       :
					inet_ntoa(gw)));

			if (strstr(buff, "br0"))
			{
				ret += websWrite(wp, "%-16s%-16s%-16s%-6s%-6d %-2d %7d LAN %s\n",
				sdest, sgw,
				inet_ntoa(mask),
				flags, metric, ref, use, buff);
			}
			else if(!strstr(buff, "lo"))
			{
				ret += websWrite(wp, "%-16s%-16s%-16s%-6s%-6d %-2d %7d WAN %s\n",
				sdest, sgw,
				inet_ntoa(mask),
				flags, metric, ref, use, buff);
			}
		}
		nl++;
	}
	fclose(fp);

	return 0;
}

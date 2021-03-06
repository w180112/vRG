#ifndef _VRG_H_
#define _VRG_H_

#include <rte_common.h>
#include <rte_atomic.h>
#include <common.h>
#include "dhcp_codec.h"
#include "pppd.h"

#define LINK_DOWN           0x0
#define LINK_UP             0x1

enum {
    CLI_QUIT = 0,
    CLI_DISCONNECT,
    CLI_CONNECT,
    CLI_DHCP_START,
    CLI_DHCP_STOP,
};

/* vRG system data structure */
typedef struct {
    U8 				        cur_user;       /* pppoe alive user count */
    U16 				    user_count;     /* total vRG subscriptor */
    U16                     base_vlan;      /* started vlan id */
    volatile BOOL	        quit_flag;      /* vRG quit flag */
	U32						lan_ip;         /* vRG LAN side ip */
    FILE 					*fp;
    struct cmdline 			*cl;
    struct rte_ether_addr 	hsi_wan_src_mac;/* vRG WAN side mac addr */
    struct rte_ether_addr 	hsi_lan_mac;    /* vRG LAN side mac addr */
    PPP_INFO_t              *ppp_ccb;       /* pppoe control block */
    dhcp_ccb_t              *dhcp_ccb;      /* dhcp control block */
    struct rte_timer 	    link;           /* for physical link checking timer */
}__rte_cache_aligned VRG_t;

struct lcore_map {
	U8 ctrl_thread;
	U8 wan_thread;
	U8 down_thread;
	U8 lan_thread;
	U8 up_thread;
	U8 gateway_thread;
	U8 timer_thread;
};

extern VRG_t vrg_ccb;

#endif
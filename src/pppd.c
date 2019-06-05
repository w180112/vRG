/*\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\
  PPPD.C

    - purpose : for ppp detection
	
  Designed by THE on Jan 14, 2019
/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\*/

#include        		<common.h>
#include 				<rte_eal.h>
#include 				<rte_ethdev.h>
#include 				<rte_cycles.h>
#include 				<rte_lcore.h>
#include 				<rte_timer.h>
#include 				<rte_ethdev.h>
#include 				<rte_ether.h>
#include 				<rte_log.h>
#include 				<linux/ethtool.h>

#include				<rte_memcpy.h>
#include 				<rte_flow.h>
#include 				"pppd.h"
#include				"fsm.h"
#include 				"dpdk_send_recv.h"
#include 				"flow_rules.h"

#define 				RING_SIZE 		16384
#define 				NUM_MBUFS 		8191
#define 				MBUF_CACHE_SIZE 512
#define 				BURST_SIZE 		32

BOOL					ppp_testEnable = FALSE;
U32						ppp_ttl;
U32						ppp_interval;
U16						ppp_init_delay;
uint8_t					ppp_max_msg_per_query;

U8 						PORT_BIT_MAP(tPPP_PORT ports[]);
tPPP_PORT				ppp_ports[USER]; //port is 1's based

struct rte_mempool 		*mbuf_pool;
struct rte_ring 		*rte_ring,*decap;

extern int 				timer_loop(__attribute__((unused)) void *arg);
extern int 				rte_ethtool_get_drvinfo(uint16_t port_id, struct ethtool_drvinfo *drvinfo);
extern STATUS			PPP_FSM(struct rte_timer *ppp, tPPP_PORT *port_ccb, U16 event);
extern STATUS 			parse_cmd(char *cmd, size_t cmd_len);

unsigned char 			*wan_mac;
struct rte_flow 		*flow;
int 					log_type;
FILE 					*fp;

int main(int argc, char **argv)
{
	uint16_t 				portid;
	uint16_t 				user_id_length, passwd_length;
	struct ethtool_drvinfo 	info;
	
	if (argc < 7) {
		puts("Too less parameter.");
		puts("Type ./pppoeclient <username> <password> <eal_options>");
		return ERROR;
	}

	int ret = rte_eal_init(argc-3,argv+3);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "rte initlize fail.");

	fp = fopen("./pppoeclient.log","a+");

	if (rte_lcore_count() < 6)
		rte_exit(EXIT_FAILURE, "We need at least 6 cores.\n");
	if (rte_eth_dev_count_avail() < 2)
		rte_exit(EXIT_FAILURE, "We need at least 2 eth ports.\n");

	/* init users and ports info */
	for(int i=0; i<USER; i++) {
		user_id_length = strlen(argv[1]);
		passwd_length = strlen(argv[2]);
		rte_eth_macaddr_get(0,(struct ether_addr *)ppp_ports[i].lan_mac);
		ppp_ports[i].user_id = (unsigned char *)malloc(user_id_length+1);
		ppp_ports[i].passwd = (unsigned char *)malloc(passwd_length+1);
		rte_memcpy(ppp_ports[i].user_id,argv[1],user_id_length);
		rte_memcpy(ppp_ports[i].passwd,argv[2],passwd_length);
		ppp_ports[i].user_id[user_id_length] = '\0';
		ppp_ports[i].passwd[passwd_length] = '\0';
	}
	
	wan_mac = (unsigned char *)malloc(ETH_ALEN);
	rte_eth_macaddr_get(1,(struct ether_addr *)wan_mac);

	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
	rte_ring = rte_ring_create("state_machine",RING_SIZE,rte_socket_id(),0);
	decap = rte_ring_create("decapsulation",RING_SIZE,rte_socket_id(),0);

	/* Initialize all ports. */
	RTE_ETH_FOREACH_DEV(portid) {
		memset(&info, 0, sizeof(info));
		if (rte_ethtool_get_drvinfo(portid, &info)) {
			printf("Error getting info for port %i\n", portid);
			return -1;
		}
		printf("Port %i driver: %s (ver: %s)\n", portid, info.driver, info.version);
		printf("firmware-version: %s\n", info.fw_version);
		printf("bus-info: %s\n", info.bus_info);
		if (PPP_PORT_INIT(portid) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n",portid);
	}

	signal(SIGTERM,(__sighandler_t)PPP_bye);
	signal(SIGINT,(__sighandler_t)PPP_int);

	/* init RTE timer library */
	rte_timer_subsystem_init();

	/* init timer structures */
	for(int i=0; i<USER; i++) {
		rte_timer_init(&(ppp_ports[i].pppoe));
		rte_timer_init(&(ppp_ports[i].ppp));
		rte_timer_init(&(ppp_ports[i].nat));
		ppp_ports[i].data_plane_start = FALSE;
	}

	rte_eal_remote_launch((lcore_function_t *)ppp_recvd,NULL,1);
	rte_eal_remote_launch((lcore_function_t *)decapsulation,NULL,2);
	rte_eal_remote_launch((lcore_function_t *)timer_loop,NULL,3);
	rte_eal_remote_launch((lcore_function_t *)gateway,NULL,4);
	rte_eal_remote_launch((lcore_function_t *)control_plane,NULL,5);

	char *cmd = NULL;
	size_t cmd_len = 0;
	sleep(2);
	for(;;) {
		puts("PPPoE Clinet bash #");
		getline(&cmd, &cmd_len, stdin);
		if (parse_cmd(cmd,cmd_len) == FALSE) {
			puts("Not a valid command");
			continue;
		}
	}
	rte_eal_mp_wait_lcore();
    return 0;
}

int control_plane(void)
{
	if (pppdInit() == ERROR)
		return ERROR;
	if (ppp_init() == ERROR)
		return ERROR;
	return 0;
}

/*---------------------------------------------------------
 * ppp_bye : signal handler for INTR-C only
 *--------------------------------------------------------*/
void PPP_bye(void)
{
    printf("bye!\n");
    free(wan_mac);
    rte_ring_free(rte_ring);
    fclose(fp);
    exit(0);
}

/*---------------------------------------------------------
 * ppp_int : signal handler for INTR-C only
 *--------------------------------------------------------*/
void PPP_int(void)
{
    printf("pppoe client interupt!\n");
    for(int i=0; i<USER; i++) { 
    	switch(ppp_ports[i].phase) {
    		case PPPOE_PHASE: 
    			break;
    		case LCP_PHASE:
    			ppp_ports[i].cp = 0;
    			PPP_FSM(&(ppp_ports[i].ppp),&ppp_ports[i],E_CLOSE);
    			break;
    		case DATA_PHASE:
    			ppp_ports[i].phase--;
    			ppp_ports[i].data_plane_start = FALSE;
    		case IPCP_PHASE:
    			ppp_ports[i].cp = 1;
    			PPP_FSM(&(ppp_ports[i].ppp),&ppp_ports[i],E_CLOSE);
    			break;
    		default:
    			;
    	}
    }
}

/**************************************************************
 * pppdInit: 
 *
 **************************************************************/
int pppdInit(void)
{	
	ppp_interval = (uint32_t)(3*SEC); 
    
    //--------- default of all ports ----------
    for(int i=0; i<USER; i++) {
		ppp_ports[i].enable = TRUE;
		ppp_ports[i].query_cnt = 1;
		ppp_ports[i].ppp_phase[0].state = S_INIT;
		ppp_ports[i].ppp_phase[1].state = S_INIT;
		ppp_ports[i].port = 0;
		
		ppp_ports[i].imsg_cnt =
		ppp_ports[i].err_imsg_cnt =
		ppp_ports[i].omsg_cnt = 0;
		ppp_ports[i].ipv4 = 0;
		ppp_ports[i].ipv4_gw = 0;
		ppp_ports[i].primary_dns = 0;
		ppp_ports[i].second_dns = 0;
		ppp_ports[i].phase = END_PHASE;
		ppp_ports[i].is_pap_auth = TRUE;
		memcpy(ppp_ports[i].src_mac,wan_mac,ETH_ALEN);
	}
    
	sleep(1);
	ppp_testEnable = TRUE; //to let driver ppp msg come in ...
	puts("============ pppoe init successfully ==============");
	return 0;
}
            
/***************************************************************
 * pppd : 
 *
 ***************************************************************/
int ppp_init(void)
{
	tPPP_MBX			*mail[BURST_SIZE];
	int 				cp;
	uint16_t			event;
	uint16_t			burst_size, max_retransmit = MAX_RETRAN;
	uint16_t			recv_type;
	struct ethhdr 		eth_hdr;
	pppoe_header_t 		pppoe_header;
	ppp_payload_t		ppp_payload;
	ppp_lcp_header_t	ppp_lcp;
	ppp_lcp_options_t	*ppp_lcp_options = (ppp_lcp_options_t *)malloc(40*sizeof(char));
	
	ppp_ports[0].phase = PPPOE_PHASE;
    if (build_padi(&(ppp_ports[0].pppoe),&(ppp_ports[0]),&max_retransmit) == FALSE)
    	goto out;
    rte_timer_reset(&(ppp_ports[0].pppoe),rte_get_timer_hz(),PERIODICAL,3,(rte_timer_cb_t)build_padi,&max_retransmit);
	for(;;){
	    burst_size = control_plane_dequeue(mail);
	    for(int i=0; i<burst_size; i++) {
	    	recv_type = *(uint16_t *)mail[i];
			switch(recv_type){
			case IPC_EV_TYPE_TMR:
				break;
		
			case IPC_EV_TYPE_DRV:
				if (PPP_decode_frame(mail[i],&eth_hdr,&pppoe_header,&ppp_payload,&ppp_lcp,ppp_lcp_options,&event,&(ppp_ports[0].ppp),&ppp_ports[0]) == FALSE) {
					ppp_ports[0].err_imsg_cnt++;
					continue;
				}
				if (eth_hdr.h_proto == htons(ETH_P_PPP_DIS)) {
					pppoe_phase_t pppoe_phase;
					pppoe_phase.eth_hdr = &eth_hdr;
					pppoe_phase.pppoe_header = &pppoe_header;
					pppoe_phase.pppoe_header_tag = (pppoe_header_tag_t *)((pppoe_header_t *)((struct ethhdr *)mail[i]->refp + 1) + 1);
					pppoe_phase.max_retransmit = MAX_RETRAN;

					switch(pppoe_header.code) {
					case PADO:
						rte_timer_stop(&(ppp_ports[0].pppoe));
						rte_memcpy(ppp_ports[0].src_mac,eth_hdr.h_dest,6);
						rte_memcpy(ppp_ports[0].dst_mac,eth_hdr.h_source,6);
						if (build_padr(&(ppp_ports[0].pppoe),&(ppp_ports[0]),&pppoe_phase) == FALSE)
							goto out;
						rte_timer_reset(&(ppp_ports[0].pppoe),rte_get_timer_hz(),PERIODICAL,3,(rte_timer_cb_t)build_padr,&pppoe_phase);
						continue;
					case PADS:
						rte_timer_stop(&(ppp_ports[0].pppoe));
						ppp_ports[0].session_id = pppoe_header.session_id;
						ppp_ports[0].cp = 0;
    					for (int i=0; i<2; i++) {
    						ppp_ports[0].ppp_phase[i].eth_hdr = &eth_hdr;
    						ppp_ports[0].ppp_phase[i].pppoe_header = &pppoe_header;
    						ppp_ports[0].ppp_phase[i].ppp_payload = &ppp_payload;
    						ppp_ports[0].ppp_phase[i].ppp_lcp = &ppp_lcp;
    						ppp_ports[0].ppp_phase[i].ppp_lcp_options = ppp_lcp_options;
   						}
    					PPP_FSM(&(ppp_ports[0].ppp),&ppp_ports[0],E_OPEN);
						continue;
					case PADT:
						if (build_padt(&(ppp_ports[0])) == FALSE) {
							goto out;
						}
						puts("Connection disconnected.");
						goto out;
					case PADM:
						RTE_LOG(INFO,EAL,"recv active discovery message");
						//puts("recv active discovery message");
						continue;
					default:
						puts("Unknown PPPoE discovery type.");
						goto out;
					}
				}
				ppp_ports[0].ppp_phase[0].ppp_lcp_options = ppp_lcp_options;
				ppp_ports[0].ppp_phase[1].ppp_lcp_options = ppp_lcp_options;
				if (ppp_payload.ppp_protocol == htons(AUTH_PROTOCOL)) {
					if (ppp_lcp.code == AUTH_NAK) {
						goto out;
					}
					else if (ppp_lcp.code == AUTH_ACK) {
						ppp_ports[0].cp = 1;
						PPP_FSM(&(ppp_ports[0].ppp),&ppp_ports[0],E_OPEN);
						continue;
					}
				}
				cp = (ppp_payload.ppp_protocol == htons(IPCP_PROTOCOL)) ? 1 : 0;
				ppp_ports[0].cp = cp;
				PPP_FSM(&(ppp_ports[0].ppp),&ppp_ports[0],event);
				break;
			case IPC_EV_TYPE_CLI:
				break;
			case IPC_EV_TYPE_MAP:
				break;
			default:
		    	;
		    }
		    mail[i] = NULL;
		}
    }
out:
	kill(getpid(), SIGTERM);
	return ERROR;
}

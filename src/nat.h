#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_memcpy.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_arp.h>
#include <rte_icmp.h>
#include <rte_flow.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>
#include <rte_timer.h>
#include <pthread.h>
#include <stdio.h>
#include <rte_atomic.h>
#include <rte_memcpy.h>
#include "pppd.h"

static inline void nat_icmp_learning(struct rte_ether_hdr *eth_hdr, struct ipv4_hdr *ip_hdr, struct icmp_hdr *icmphdr, uint32_t *new_port_id, addr_table_t addr_table[]);
static inline void nat_udp_learning(struct rte_ether_hdr *eth_hdr, struct ipv4_hdr *ip_hdr, struct udp_hdr *udphdr, uint32_t *new_port_id, addr_table_t addr_table[]);
static inline void nat_tcp_learning(struct rte_ether_hdr *eth_hdr, struct ipv4_hdr *ip_hdr, struct tcp_hdr *tcphdr, uint32_t *new_port_id, addr_table_t addr_table[]);

static inline void nat_icmp_learning(struct rte_ether_hdr *eth_hdr, struct ipv4_hdr *ip_hdr, struct icmp_hdr *icmphdr, uint32_t *new_port_id, addr_table_t addr_table[])
{
	*new_port_id = rte_be_to_cpu_16(icmphdr->icmp_ident + (ip_hdr->src_addr) % 10000);
	if (*new_port_id > 0xffff)
		*new_port_id = *new_port_id / 0xffff + 1000;
	for(int j=1000,shift=0; j<65535; j++) {
		if (likely(addr_table[*new_port_id].is_fill == 1)) {
			if (likely(addr_table[*new_port_id].src_ip == ip_hdr->src_addr && addr_table[*new_port_id].dst_ip == ip_hdr->dst_addr))
				return;
			shift++;
			(*new_port_id)++;
		}
		else {
			rte_wmb();
			addr_table[*new_port_id].is_fill = 1;
			//port_ccb->addr_table[*new_port_id].shift = shift;
			break;
		}
	}
	#ifdef _DP_DBG
	puts("learning new icmp nat rule");
	#endif
	rte_memcpy(addr_table[*new_port_id].mac_addr,eth_hdr->s_addr.addr_bytes,RTE_ETHER_ADDR_LEN);
	addr_table[*new_port_id].src_ip = ip_hdr->src_addr;
	addr_table[*new_port_id].dst_ip = ip_hdr->dst_addr; 
	addr_table[*new_port_id].port_id = icmphdr->icmp_ident;
}

static inline void nat_udp_learning(struct rte_ether_hdr *eth_hdr, struct ipv4_hdr *ip_hdr, struct udp_hdr *udphdr, uint32_t *new_port_id, addr_table_t addr_table[])
{
	*new_port_id = rte_be_to_cpu_16(udphdr->src_port + (ip_hdr->src_addr) % 10000);
	if (*new_port_id > 0xffff)
		*new_port_id = *new_port_id / 0xffff + 1000;
	for(int j=1000,shift=0; j<65535; j++) {
		if (likely(addr_table[*new_port_id].is_fill == 1)) {
			if (likely(addr_table[*new_port_id].src_ip == ip_hdr->src_addr && addr_table[*new_port_id].dst_ip == ip_hdr->dst_addr))
				return;
			shift++;
			(*new_port_id)++;
		}
		else {
			rte_wmb();
			addr_table[*new_port_id].is_fill = 1;
			//port_ccb->addr_table[*new_port_id].shift = shift;
			break;
		}
	}
	#ifdef _DP_DBG
	puts("learning new udp nat rule");
	#endif
	rte_memcpy(addr_table[*new_port_id].mac_addr,eth_hdr->s_addr.addr_bytes,RTE_ETHER_ADDR_LEN);
	addr_table[*new_port_id].src_ip = ip_hdr->src_addr;
	addr_table[*new_port_id].dst_ip = ip_hdr->dst_addr; 
	addr_table[*new_port_id].port_id = udphdr->src_port;
}

static inline void nat_tcp_learning(struct rte_ether_hdr *eth_hdr, struct ipv4_hdr *ip_hdr, struct tcp_hdr *tcphdr, uint32_t *new_port_id, addr_table_t addr_table[])
{
	*new_port_id = rte_be_to_cpu_16(tcphdr->src_port + (ip_hdr->src_addr) % 10000);
	if (*new_port_id > 0xffff)
		*new_port_id = *new_port_id / 0xffff + 1000;
	for(int j=1000,shift=0; j<65535; j++) {
		if (likely(addr_table[*new_port_id].is_fill == 1)) {
			if (likely(addr_table[*new_port_id].src_ip == ip_hdr->src_addr && addr_table[*new_port_id].dst_ip == ip_hdr->dst_addr))
				return;
			shift++;
			(*new_port_id)++;
		}
		else {
			rte_wmb();
			addr_table[*new_port_id].is_fill = 1;
			//port_ccb->addr_table[*new_port_id].shift = shift;
			break;
		}
	}
	#ifdef _DP_DBG
	puts("learning new tcp nat rule");
	#endif
	rte_memcpy(addr_table[*new_port_id].mac_addr,eth_hdr->s_addr.addr_bytes,RTE_ETHER_ADDR_LEN);
	addr_table[*new_port_id].src_ip = ip_hdr->src_addr;
	addr_table[*new_port_id].dst_ip = ip_hdr->dst_addr; 
	addr_table[*new_port_id].port_id = tcphdr->src_port;
}

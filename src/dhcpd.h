/*\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\
  DHCPD.H

  Designed by THE on MAR 21, 2021
/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\/\*/

#ifndef _DHCPD_H_
#define _DHCPD_H_

extern int dhcpd(struct rte_mbuf *single_pkt, struct rte_ether_hdr *eth_hdr, vlan_header_t *vlan_header, struct rte_ipv4_hdr *ip_hdr, struct rte_udp_hdr *udp_hdr, uint16_t user_index);
extern int dhcp_init();

#endif
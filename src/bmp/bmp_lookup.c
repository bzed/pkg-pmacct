/*  
    pmacct (Promiscuous mode IP Accounting package)
    pmacct is Copyright (C) 2003-2017 by Paolo Lucente
*/

/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

/* defines */
#define __BMP_LOOKUP_C

/* includes */
#include "pmacct.h"
#include "addr.h"
#include "../bgp/bgp.h"
#include "bmp.h"

void bmp_srcdst_lookup(struct packet_ptrs *pptrs)
{
  bgp_srcdst_lookup(pptrs, FUNC_TYPE_BMP);
}

struct bgp_peer *bgp_lookup_find_bmp_peer(struct sockaddr *sa, struct xflow_status_entry *xs_entry, u_int16_t l3_proto, int compare_bgp_port)
{
  struct bgp_peer *peer;
  u_int32_t peer_idx, *peer_idx_ptr;
  int peers_idx;

  peer_idx = 0; peer_idx_ptr = NULL;
  if (xs_entry) {
    if (l3_proto == ETHERTYPE_IP) {
      peer_idx = xs_entry->peer_v4_idx;
      peer_idx_ptr = &xs_entry->peer_v4_idx;
    }
#if defined ENABLE_IPV6
    else if (l3_proto == ETHERTYPE_IPV6) {
      peer_idx = xs_entry->peer_v6_idx;
      peer_idx_ptr = &xs_entry->peer_v6_idx;
    }
#endif
  }

  if (xs_entry && peer_idx) {
    if (!sa_addr_cmp(sa, &bmp_peers[peer_idx].self.addr) || !sa_addr_cmp(sa, &bmp_peers[peer_idx].self.id))
      peer = &bmp_peers[peer_idx].self;
    /* If no match then let's invalidate the entry */
    else {
      *peer_idx_ptr = 0;
      peer = NULL;
    }
  }
  else {
    for (peer = NULL, peers_idx = 0; peers_idx < config.nfacctd_bmp_max_peers; peers_idx++) {
      if (!sa_addr_cmp(sa, &bmp_peers[peers_idx].self.addr) || !sa_addr_cmp(sa, &bmp_peers[peers_idx].self.id)) {
        peer = &bmp_peers[peers_idx].self;
        if (xs_entry && peer_idx_ptr) *peer_idx_ptr = peers_idx;
        break;
      }
    }
  }

  return peer;
}

u_int32_t bmp_route_info_modulo_pathid(struct bgp_peer *peer, path_id_t *path_id, int per_peer_buckets)
{
  struct bgp_misc_structs *bms = bgp_select_misc_db(peer->type);
  struct bmp_peer *bmpp = peer->bmp_se;
  path_id_t local_path_id = 1;
  int fd = 0;

  if (path_id && *path_id) local_path_id = *path_id;

  if (peer->fd) fd = peer->fd;
  else {
    if (bmpp && bmpp->self.fd) fd = bmpp->self.fd;
  }

  return (((fd * per_peer_buckets) +
          ((local_path_id - 1) % per_peer_buckets)) %
          (bms->table_peer_buckets * per_peer_buckets));
}

int bgp_lookup_node_match_cmp_bmp(struct bgp_info *info, struct node_match_cmp_term2 *nmct2)
{
  struct bmp_peer *bmpp = info->peer->bmp_se;
  struct bgp_peer *peer_local = &bmpp->self;
  int no_match = FALSE;

  if (peer_local == nmct2->peer) {
    if (nmct2->safi == SAFI_MPLS_VPN) no_match++;
    if (nmct2->peer->cap_add_paths) no_match++;

    if (nmct2->safi == SAFI_MPLS_VPN) {
      if (info->extra && !memcmp(&info->extra->rd, &nmct2->rd, sizeof(rd_t))) no_match--;
    }

    if (nmct2->peer->cap_add_paths) {
      if (info->attr) {
        if (info->attr->mp_nexthop.family == nmct2->peer_dst_ip->family) {
          if (!memcmp(&info->attr->mp_nexthop, &nmct2->peer_dst_ip, HostAddrSz)) no_match--;
        }
        else if (info->attr->nexthop.s_addr && nmct2->peer_dst_ip->family == AF_INET) {
          if (info->attr->nexthop.s_addr == nmct2->peer_dst_ip->address.ipv4.s_addr) no_match--;
        }
      }
    }

    if (!no_match) return FALSE;
  }

  return TRUE;
}

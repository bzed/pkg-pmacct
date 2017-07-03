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

#ifndef _BGP_MSG_H_
#define _BGP_MSG_H_

/* prototypes */
#if (!defined __BGP_MSG_C)
#define EXT extern
#else
#define EXT
#endif
EXT int bgp_max_msglen_check(u_int32_t);
EXT int bgp_marker_check(struct bgp_header *, int);
EXT int bgp_parse_msg(struct bgp_peer *, time_t, int);
EXT int bgp_parse_open_msg(struct bgp_msg_data *, char *, time_t, int);
EXT int bgp_parse_update_msg(struct bgp_msg_data *, char *);
EXT int bgp_parse_notification_msg(struct bgp_msg_data *, char *, u_int8_t *, u_int8_t *, char *, u_int8_t);
EXT int bgp_write_keepalive_msg(char *);
EXT int bgp_write_open_msg(char *, char *, int, struct bgp_peer *);
EXT int bgp_write_notification_msg(char *, int, u_int8_t, u_int8_t, char *);
EXT int bgp_attr_parse(struct bgp_peer *, struct bgp_attr *, char *, int, struct bgp_nlri *, struct bgp_nlri *);
EXT int bgp_attr_parse_community(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_int8_t);
EXT int bgp_attr_parse_ecommunity(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_int8_t);
EXT int bgp_attr_parse_lcommunity(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_int8_t);
EXT int bgp_attr_parse_aspath(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_int8_t);
EXT int bgp_attr_parse_as4path(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_int8_t, struct aspath **);
EXT int bgp_attr_parse_nexthop(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_int8_t);
EXT int bgp_attr_parse_med(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_char);
EXT int bgp_attr_parse_local_pref(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_char);
EXT int bgp_attr_parse_origin(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, u_char);
EXT int bgp_attr_parse_mp_reach(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, struct bgp_nlri *);
EXT int bgp_attr_parse_mp_unreach(struct bgp_peer *, u_int16_t, struct bgp_attr *, char *, struct bgp_nlri *);
EXT int bgp_nlri_parse(struct bgp_msg_data *, void *, struct bgp_nlri *);
EXT int bgp_process_update(struct bgp_msg_data *, struct prefix *, void *, afi_t, safi_t, rd_t *, path_id_t *, char *);
EXT int bgp_process_withdraw(struct bgp_msg_data *, struct prefix *, void *, afi_t, safi_t, rd_t *, path_id_t *, char *);
#undef EXT
#endif 

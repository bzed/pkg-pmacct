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

/* includes */
#include <stdarg.h>
#include <sys/stat.h>

/* defines */
#define LOGSTRLEN LONGSRVBUFLEN 

struct _facility_map {
  char string[10];
  int num;
};

static const struct _facility_map facility_map[] = {
	{"auth", LOG_AUTH},
	{"mail", LOG_MAIL},
	{"daemon", LOG_DAEMON},
	{"kern", LOG_KERN},
	{"user", LOG_USER},
	{"local0", LOG_LOCAL0},
	{"local1", LOG_LOCAL1},
	{"local2", LOG_LOCAL2},
	{"local3", LOG_LOCAL3},
	{"local4", LOG_LOCAL4},
	{"local5", LOG_LOCAL5},
	{"local6", LOG_LOCAL6},
	{"local7", LOG_LOCAL7},
	{"-1", -1},
};

struct log_notification {
  time_t stamp;
  u_int8_t knob;
  int timeout;
};

struct _log_notifications {
  struct log_notification max_classifiers;
  struct log_notification bgp_peers_throttling;
  struct log_notification bmp_peers_throttling;
  struct log_notification geoip_ipv4_file_null;
  struct log_notification geoip_ipv6_file_null;
#if defined (WITH_NDPI)
  struct log_notification ndpi_cache_full;
  struct log_notification ndpi_tmp_frag_warn;
#endif
};

/* prototypes */
#if (!defined __LOG_C)
#define EXT extern
#else
#define EXT
#endif
EXT void Log(short int, char *, ...);
EXT int parse_log_facility(const char *);
EXT void log_notification_init(struct log_notification *);
EXT void log_notifications_init(struct _log_notifications *);
EXT int log_notification_set(struct log_notification *, time_t, int);
EXT int log_notification_unset(struct log_notification *);
EXT int log_notification_isset(struct log_notification *, time_t);

/* global vars */
EXT struct _log_notifications log_notifications;
#undef EXT

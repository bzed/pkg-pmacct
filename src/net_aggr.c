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

#define __NET_AGGR_C

/* includes */
#include "pmacct.h"
#include "nfacctd.h"
#include "pmacct-data.h"
#include "plugin_hooks.h"
#include "net_aggr.h"
#include "addr.h"
#include "jhash.h"

void load_networks(char *filename, struct networks_table *nt, struct networks_cache *nc)
{
  load_networks4(filename, nt, nc);
#if defined ENABLE_IPV6
  load_networks6(filename, nt, nc);
#endif
}

void load_networks4(char *filename, struct networks_table *nt, struct networks_cache *nc)
{
  FILE *file;
  struct networks_table tmp, *tmpt = &tmp; 
  struct networks_table bkt;
  struct networks_table_metadata *mdt = NULL;
  char buf[SRVBUFLEN], *bufptr, *delim, *peer_as, *as, *net, *mask, *nh;
  int rows, eff_rows = 0, j, buflen, fields, prev[128];
  unsigned int index, fake_row = 0;
  struct stat st;

  /* dummy & broken on purpose */
  memset(&dummy_entry, 0, sizeof(struct networks_table_entry));
  dummy_entry.masknum = 255;

  memset(&bkt, 0, sizeof(bkt));
  memset(&tmp, 0, sizeof(tmp));
  memset(&st, 0, sizeof(st));
  default_route_in_networks4_table = FALSE;

  /* backing up pre-existing table and cache */ 
  if (nt->num) {
    bkt.table = nt->table;
    bkt.num = nt->num;
    bkt.timestamp = nt->timestamp;

    nt->table = NULL;
    nt->num = 0;
    nt->timestamp = 0;
  }

  if (filename) {
    if ((file = fopen(filename,"r")) == NULL) {
      if (!(config.nfacctd_net & NF_NET_KEEP && config.nfacctd_as & NF_AS_KEEP)) {
        Log(LOG_WARNING, "WARN ( %s/%s ): [%s] file not found.\n", config.name, config.type, filename);
	return;
      }

      Log(LOG_ERR, "ERROR ( %s/%s ): [%s] file not found.\n", config.name, config.type, filename);
      goto handle_error;
    }
    else {
      rows = 0;
      /* 1st step: count rows for table allocation */
      while (!feof(file)) {
	    if (fgets(buf, SRVBUFLEN, file) && !iscomment(buf)) rows++;
      }
      /* 2nd step: loading data into a temporary table */
      if (!freopen(filename, "r", file)) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] freopen() failed (%s).\n", config.name, config.type, filename, strerror(errno));
        goto handle_error;
      }

      /* We have no (valid) rows. We build a zeroed single-row table aimed to complete
	 successfully any further lookup */ 
      if (!rows) {
	fake_row = TRUE;
	rows++;
      }

      nt->table = malloc(rows*sizeof(struct networks_table_entry)); 
      if (!nt->table) {
	Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Networks Table.\n", config.name, config.type, filename);
	goto handle_error;
      }

      tmpt->table = malloc(rows*sizeof(struct networks_table_entry));
      if (!tmpt->table) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Temporary Networks Table.\n", config.name, config.type, filename);
        goto handle_error;
      }

      memset(nt->table, 0, rows*sizeof(struct networks_table_entry));
      memset(tmpt->table, 0, rows*sizeof(struct networks_table_entry));
      rows = 1;

      while (!feof(file)) {
	bufptr = buf;
	memset(buf, 0, SRVBUFLEN);

        if (fgets(buf, SRVBUFLEN, file)) { 
	  if (iscomment(buf)) continue;

	  for (fields = 0, delim = strchr(bufptr, ','); delim; fields++) {
	    bufptr = delim+1;
	    delim = strchr(bufptr, ',');
	  }

	  bufptr = buf;

#if defined ENABLE_PLABEL
          if (fields >= 3) {
            char *plabel, *endptr;

	    memset(tmpt->table[eff_rows].plabel, 0, PREFIX_LABEL_LEN);

	    /* If the specific plugin does not support IP prefix labels (yet?)
	       then just skip the field */ 
	    if (config.type_id != PLUGIN_ID_CORE && config.type_id != PLUGIN_ID_NFPROBE &&
		config.type_id != PLUGIN_ID_SFPROBE && config.type_id != PLUGIN_ID_TEE) {
              delim = strchr(bufptr, ',');
              plabel = bufptr;
              *delim = '\0';
              bufptr = delim+1;
              strlcpy(tmpt->table[eff_rows].plabel, plabel, PREFIX_LABEL_LEN);
	    }
	    else {
              delim = strchr(bufptr, ',');
              *delim = '\0';
              bufptr = delim+1;
	    }
          }
#else
          if (fields >= 3) {
            delim = strchr(bufptr, ',');
            *delim = '\0';
            bufptr = delim+1;
	  }
#endif

	  if (fields >= 2) {
            char *endptr;

            delim = strchr(bufptr, ',');
            nh = bufptr;
            *delim = '\0';
            bufptr = delim+1;
            str_to_addr(nh, &tmpt->table[eff_rows].nh);
	  }
	  else memset(&tmpt->table[eff_rows].nh, 0, sizeof(struct host_addr));

	  if (fields >= 1) {
	    char *endptr, *endptr2;

	    peer_as = as = NULL;
	    delim = strchr(bufptr, ',');
	    if (delim) {
	      as = bufptr;
	      *delim = '\0';
	      bufptr = delim+1;

	      delim = strchr(as, '_');
	      if (delim) {
	        *delim = '\0';
	        peer_as = as;
	        as = delim+1;
	      }
	    }
	    else tmpt->table[eff_rows].peer_as = 0;

	    if (as) tmpt->table[eff_rows].as = strtoul(as, &endptr, 10);
	    else tmpt->table[eff_rows].as = 0;

	    if (peer_as) tmpt->table[eff_rows].peer_as = strtoul(peer_as, &endptr2, 10);
	    else tmpt->table[eff_rows].peer_as = 0;
	  }
	  else {
	    tmpt->table[eff_rows].peer_as = 0;
	    tmpt->table[eff_rows].as = 0;
	  }

	  if (!sanitize_buf_net(filename, bufptr, rows)) {
	    delim = strchr(bufptr, '/');
	    *delim = '\0';
	    net = bufptr;
	    mask = delim+1;

            if (!inet_aton(net, (struct in_addr *) &tmpt->table[eff_rows].net)) {
	      memset(&tmpt->table[eff_rows], 0, sizeof(struct networks_table_entry));
	      goto cycle_end;
	    }

	    buflen = strlen(mask);
	    for (j = 0; j < buflen; j++) {
	      if (!isdigit(mask[j])) {
		Log(LOG_ERR, "ERROR ( %s/%s ): [%s:%u] Invalid network mask '%s'.\n", config.name, config.type, filename, rows, mask);
	        memset(&tmpt->table[eff_rows], 0, sizeof(struct networks_table_entry));
		goto cycle_end;
	      }
	    }
	    index = atoi(mask); 
	    if (index > 32) {
	      Log(LOG_ERR, "ERROR ( %s/%s ): [%s:%u] Invalid network mask '%d'.\n", config.name, config.type, filename, rows, index);
	      memset(&tmpt->table[eff_rows], 0, sizeof(struct networks_table_entry));
	      goto cycle_end;
	    }

	    tmpt->table[eff_rows].net = ntohl(tmpt->table[eff_rows].net);
	    tmpt->table[eff_rows].mask = (index == 32) ? 0xffffffffUL : ~(0xffffffffUL >> index);
	    tmpt->table[eff_rows].masknum = index;
	    tmpt->table[eff_rows].net &= tmpt->table[eff_rows].mask; /* enforcing mask on given network */

	    eff_rows++;
	  } 
	}

	cycle_end:
	rows++;
      }
      fclose(file);
      stat(filename, &st);

      /* We have no (valid) rows. We build a zeroed single-row table aimed to complete
         successfully any further lookup */
      if (!eff_rows) eff_rows++;

      /* 3rd step: sorting table */
      merge_sort(filename, tmpt->table, 0, eff_rows);
      tmpt->num = eff_rows;

      /* 4th step: collecting informations in the sorted table;
         we wish to handle networks-in-networks hierarchically */

      /* 4a building hierarchies */
      mdt = malloc(tmpt->num*sizeof(struct networks_table_metadata));
      if (!mdt) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Metadata Networks Table.\n", config.name, config.type, filename);
        goto handle_error;
      }
      memset(mdt, 0, tmpt->num*sizeof(struct networks_table_metadata));

      if (tmpt->num) {
        for (index = 0; index < (tmpt->num-1); index++) {
	  u_int32_t net;
	  int x;

	  for (x = index+1; x < tmpt->num; x++) {
	    net = tmpt->table[x].net;
	    net &= tmpt->table[index].mask;
	    if (net == tmpt->table[index].net) {
	      mdt[x].level++;
	      mdt[index].childs++; 
	    }
	    else break;
	  }
        } 
      }

      /* 4b retrieving root entries number */
      for (index = 0, eff_rows = 0; index < tmpt->num; index++) {
        if (mdt[index].level == 0) eff_rows++;
      }

      nt->num = eff_rows;
      /* 4c adjusting child counters: each parent has to know
         only the number of its directly attached childs and
	 not the whole hierarchy */ 
      for (index = 0; index < tmpt->num; index++) {
        int x, eff_childs = 0;

        for (x = index+1; x < tmpt->num; x++) {
	  if (mdt[index].level == mdt[x].level) break;
	  else if (mdt[index].level == (mdt[x].level-1)) eff_childs++; 
	}
	mdt[index].childs = eff_childs;
      }

      /* 5a step: building final networks table */
      for (index = 0; index < tmpt->num; index++) {
	int current, next;

        if (!index) {
	  current = 0; next = eff_rows;
	  memset(&prev, 0, 32);
	  memcpy(&nt->table[current], &tmpt->table[index], sizeof(struct networks_table_entry));
        }
	else {
	  if (mdt[index].level == mdt[index-1].level) current++; /* do nothing: we have only to copy our element */ 
	  else if (mdt[index].level > mdt[index-1].level) { /* we encountered a child */ 
	    nt->table[current].childs_table.table = &nt->table[next];
	    nt->table[current].childs_table.num = mdt[index-1].childs;
	    prev[mdt[index-1].level] = current;
	    current = next;
	    next += mdt[index-1].childs;
	  }
	  else { /* going back to parent level */
	    current = prev[mdt[index].level];
	    current++;
	  }
	  memcpy(&nt->table[current], &tmpt->table[index], sizeof(struct networks_table_entry));
        }
      }

      /* 5b step: debug and default route detection */
      index = 0;
      while (!fake_row && index < tmpt->num) {
        if (config.debug) { 
	  struct host_addr net_bin;
	  char nh_string[INET6_ADDRSTRLEN];
	  char net_string[INET6_ADDRSTRLEN];

	  addr_to_str(nh_string, &nt->table[index].nh);

	  net_bin.family = AF_INET;
	  net_bin.address.ipv4.s_addr = htonl(nt->table[index].net);
	  addr_to_str(net_string, &net_bin);

	  Log(LOG_DEBUG, "DEBUG ( %s/%s ): [%s] v4 nh: %s peer asn: %u asn: %u net: %s mask: %u\n", 
		config.name, config.type, filename, nh_string, nt->table[index].peer_as,
		nt->table[index].as, net_string, nt->table[index].masknum); 
	}
	if (!nt->table[index].mask) {
	  Log(LOG_DEBUG, "DEBUG ( %s/%s ): [%s] v4 contains a default route\n", config.name, config.type, filename);
	  default_route_in_networks4_table = TRUE;
	}
	index++;
      }

      /* 6th step: create networks cache BUT only for the first time */
      if (!nc->cache) {
        if (!config.networks_cache_entries) nc->num = NETWORKS_CACHE_ENTRIES;
        else nc->num = config.networks_cache_entries;
        nc->cache = (struct networks_cache_entry *) malloc(nc->num*sizeof(struct networks_cache_entry));
        if (!nc->cache) {
          Log(LOG_ERR, "ERROR ( %s/%s ): malloc() failed while building Networks Cache.\n", config.name, config.type);
          goto handle_error;
        }
	else Log(LOG_DEBUG, "DEBUG ( %s/%s ): [%s] IPv4 Networks Cache successfully created: %u entries.\n",
			config.name, config.type, filename, nc->num);
      }

      /* 7th step: freeing resources */
      memset(nc->cache, 0, nc->num*sizeof(struct networks_cache_entry));
      free(tmpt->table);
      free(mdt);
      if (bkt.table) free(bkt.table);

      /* 8th step: setting timestamp */
      nt->timestamp = st.st_mtime;
    }
  }

  return;

  /* 
     error handling: if we have a copy of the old table we will rollback it;
     otherwise we just take the exit lane. XXX: actually we are just able to
     recover malloc() troubles and missing files; efforts should be pushed
     in the validation of the new table.
   */
  handle_error:
  if (tmpt->table) free(tmpt->table);
  if (mdt) free(mdt);

  if (bkt.num) {
    if (!nt->table) {
      memcpy(nt, &bkt, sizeof(struct networks_table));
      Log(LOG_WARNING, "WARN ( %s/%s ): [%s] Rolling back the old Networks Table.\n", config.name, config.type, filename); 

      /* we update the timestamp to avoid loops */ 
      stat(filename, &st);
      nt->timestamp = st.st_mtime;
    }
  }
  else exit_plugin(1);
}

/* sort the (sub)array v from start to end */
void merge_sort(char *filename, struct networks_table_entry *table, int start, int end)
{
  int middle;

  /* no elements to sort */
  if ((start == end) || (start == end-1)) return;

  /* find the middle of the array, splitting it into two subarrays */
  middle = (start+end)/2;

  /* sort the subarray from start..middle */
  merge_sort(filename, table, start, middle);

  /* sort the subarray from middle..end */
  merge_sort(filename, table, middle, end);

  /* merge the two sorted halves */
  merge(filename, table, start, middle, end);
}

/* 
   merge the subarray v[start..middle] with v[middle..end], placing the
   result back into v.
*/
void merge(char *filename, struct networks_table_entry *table, int start, int middle, int end)
{
  struct networks_table_entry *v1, *v2;
  int  v1_n, v2_n, v1_index, v2_index, i, s = sizeof(struct networks_table_entry);

  v1_n = middle-start;
  v2_n = end-middle;

  v1 = malloc(v1_n*s);
  v2 = malloc(v2_n*s);

  if ((!v1) || (!v2)) Log(LOG_ERR, "ERROR ( %s/%s ): [%s] memory sold out.\n", config.name, config.type, filename); 

  for (i=0; i<v1_n; i++) memcpy(&v1[i], &table[start+i], s);
  for (i=0; i<v2_n; i++) memcpy(&v2[i], &table[middle+i], s);

  v1_index = 0;
  v2_index = 0;

  /* as we pick elements from one or the other to place back into the table */
  for (i=0; (v1_index < v1_n) && (v2_index < v2_n); i++) {
    /* current v1 element less than current v2 element? */
    if (v1[v1_index].net < v2[v2_index].net) memcpy(&table[start+i], &v1[v1_index++], s);
    else if (v1[v1_index].net == v2[v2_index].net) {
      if (v1[v1_index].mask <= v2[v2_index].mask) memcpy(&table[start+i], &v1[v1_index++], s);
      else memcpy(&table[start+i], &v2[v2_index++], s);
    }
    else memcpy(&table[start+i], &v2[v2_index++], s); 
  }

  /* clean up; either v1 or v2 may have stuff left in it */
  for (; v1_index < v1_n; i++) memcpy(&table[start+i], &v1[v1_index++], s);
  for (; v2_index < v2_n; i++) memcpy(&table[start+i], &v2[v2_index++], s);

  free(v1);
  free(v2);
}

struct networks_table_entry *binsearch(struct networks_table *nt, struct networks_cache *nc, struct host_addr *a)
{
  int low = 0, mid, high = nt->num-1;
  u_int32_t net, addrh = ntohl(a->address.ipv4.s_addr), addr = a->address.ipv4.s_addr;
  struct networks_table_entry *ret;

  ret = networks_cache_search(nc, &addr); 
  if (ret) {
    if (ret->masknum == 255) return NULL; /* dummy entry identification */
    else return ret;
  }

  while (low <= high) {
    mid = (low+high)/2;
    net = addrh;
    net &= nt->table[mid].mask;

    if (net < nt->table[mid].net) {
      high = mid-1;
      continue;
    }
    else if (net > nt->table[mid].net) {
      low = mid+1;
      continue;
    }

    /* It's assumed we've found our element */
    if (nt->table[mid].childs_table.table) {
      ret = binsearch(&nt->table[mid].childs_table, nc, a);
      if (!ret) {
	ret = &nt->table[mid];
        networks_cache_insert(nc, &addr, &nt->table[mid]);
      }
    }
    else {
      ret = &nt->table[mid];
      networks_cache_insert(nc, &addr, &nt->table[mid]);
    }
    return ret;
  }

  networks_cache_insert(nc, &addr, &dummy_entry);
  return NULL;
}

void networks_cache_insert(struct networks_cache *nc, u_int32_t *key, struct networks_table_entry *result)
{
  struct networks_cache_entry *ptr;

  ptr = &nc->cache[*key % nc->num];
  ptr->key = *key;
  ptr->result = result;
}

struct networks_table_entry *networks_cache_search(struct networks_cache *nc, u_int32_t *key)
{
  struct networks_cache_entry *ptr;

  ptr = &nc->cache[*key % nc->num];
  if (ptr->key == *key) return ptr->result;
  else return NULL;
}

void set_net_funcs(struct networks_table *nt)
{
  u_int8_t count = 0;

  memset(&net_funcs, 0, sizeof(net_funcs));

  if ((config.nfacctd_net & NF_NET_STATIC) && config.networks_mask) {
    int j, index = config.networks_mask;

    memset(nt->maskbits, 0, sizeof(nt->maskbits));
    for (j = 0; j < 4 && index >= 32; j++, index -= 32) nt->maskbits[j] = 0xffffffffU;
    if (j < 4 && index) nt->maskbits[j] = ~(0xffffffffU >> index);

    if (config.what_to_count & (COUNT_SRC_NET|COUNT_SUM_NET)) {
      net_funcs[count] = mask_static_src_ipaddr;
      count++;
    }
    if (config.what_to_count & (COUNT_DST_NET|COUNT_SUM_NET)) {
      net_funcs[count] = mask_static_dst_ipaddr;
      count++;
    }
    if (config.what_to_count & COUNT_SRC_NMASK) {
      net_funcs[count] = copy_src_mask;
      count++;
    }
    if (config.what_to_count & COUNT_DST_NMASK) {
      net_funcs[count] = copy_dst_mask;
      count++;
    }
  }

#if defined ENABLE_IPV6
  if ((!nt->num) && (!nt->num6)) goto exit_lane;
#else
  if (!nt->num) goto exit_lane;
#endif

  net_funcs[count] = init_net_funcs;
  count++;

  net_funcs[count] = search_src_ip;
  count++;

  if (config.what_to_count & (COUNT_SRC_HOST|COUNT_SUM_HOST)) {
    net_funcs[count] = search_src_host;
    count++;
  }

  if (config.nfacctd_net & NF_NET_NEW) {
    net_funcs[count] = search_src_nmask;
    count++;
  }

  if (config.nfacctd_as & NF_AS_NEW) {
    if (config.what_to_count & (COUNT_SRC_AS|COUNT_SUM_AS)) {
      net_funcs[count] = search_src_as;
      count++;
    }
  }

  if (config.nfacctd_as & NF_AS_NEW) {
    if (config.what_to_count & COUNT_PEER_SRC_AS) {
      net_funcs[count] = search_peer_src_as;
      count++;
    }
  }

  if (config.what_to_count & (COUNT_SRC_NET|COUNT_SUM_NET)) { 
    net_funcs[count] = mask_src_ipaddr;
    count++;
  }
  else {
    net_funcs[count] = clear_src_net;
    count++;
  }

  if (!(config.what_to_count & (COUNT_SRC_HOST|COUNT_SUM_HOST))) {
    net_funcs[count] = clear_src_host;
    count++;
  }
  else {
#if defined ENABLE_PLABEL
    net_funcs[count] = search_src_host_label;
    count++;
#endif
  }

  if (!(config.what_to_count & COUNT_SRC_NMASK)) {
    net_funcs[count] = clear_src_nmask;
    count++;
  }

  net_funcs[count] = search_dst_ip;
  count++;

  if (config.what_to_count & (COUNT_DST_HOST|COUNT_SUM_HOST)) {
    net_funcs[count] = search_dst_host;
    count++;
  }

  if (config.nfacctd_net & NF_NET_NEW) {
    net_funcs[count] = search_dst_nmask;
    count++;
  }

  if (config.nfacctd_as & NF_AS_NEW) {
    if (config.what_to_count & (COUNT_DST_AS|COUNT_SUM_AS)) {
      net_funcs[count] = search_dst_as;
      count++;
    }
  }

  if (config.nfacctd_as & NF_AS_NEW) {
    if (config.what_to_count & COUNT_PEER_DST_AS) {
      net_funcs[count] = search_peer_dst_as;
      count++;
    }
  }

  if (config.nfacctd_net & NF_NET_NEW) {
    if (config.what_to_count & COUNT_PEER_DST_IP) {
      net_funcs[count] = search_peer_dst_ip;
      count++;
    }
  }

  if (config.what_to_count & (COUNT_DST_NET|COUNT_SUM_NET)) {
    net_funcs[count] = mask_dst_ipaddr;
    count++;
  }
  else {
    net_funcs[count] = clear_dst_net;
    count++;
  }

  if (!(config.what_to_count & (COUNT_DST_HOST|COUNT_SUM_HOST))) {
    net_funcs[count] = clear_dst_host;
    count++;
  }
  else {
#if defined ENABLE_PLABEL
    net_funcs[count] = search_dst_host_label;
    count++;
#endif
  }

  if (!(config.what_to_count & COUNT_DST_NMASK)) {
    net_funcs[count] = clear_dst_nmask;
    count++;
  }

  assert(count < NET_FUNCS_N);  

  return;

  /* no networks_file loaded: apply masks and clean-up */
  exit_lane:

  if (config.what_to_count & (COUNT_SRC_NET|COUNT_SUM_NET)) { 
    net_funcs[count] = mask_src_ipaddr;
    count++;
  }
  else {
    net_funcs[count] = clear_src_net;
    count++;
  }

  if (!(config.what_to_count & (COUNT_SRC_HOST|COUNT_SUM_HOST))) {
    net_funcs[count] = clear_src_host;
    count++;
  }

  if (config.what_to_count & COUNT_DST_NET) { 
    net_funcs[count] = mask_dst_ipaddr;
    count++;
  } 
  else {
    net_funcs[count] = clear_dst_net;
    count++;
  }
  
  if (!(config.what_to_count & (COUNT_DST_HOST|COUNT_SUM_HOST))) {
    net_funcs[count] = clear_dst_host;
    count++;
  }

  if (!(config.what_to_count & COUNT_SRC_NMASK)) {
    net_funcs[count] = clear_src_nmask;
    count++;
  }

  if (!(config.what_to_count & COUNT_DST_NMASK)) {
    net_funcs[count] = clear_dst_nmask;
    count++;
  }

  assert(count < NET_FUNCS_N);
}

void init_net_funcs(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  memset(nfd, 0, sizeof(struct networks_file_data));
}

void clear_src_nmask(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  p->src_nmask = 0;
}

void clear_dst_nmask(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  p->dst_nmask = 0;
}

void mask_src_ipaddr(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  u_int32_t maskbits[4], addrh[4];
  u_int8_t j, mask;

  memset(maskbits, 0,sizeof(maskbits));
  mask = p->src_nmask;

  if (config.networks_no_mask_if_zero && !mask) mask = 128;

  for (j = 0; j < 4 && mask >= 32; j++, mask -= 32) maskbits[j] = 0xffffffffU;
  if (j < 4 && mask) maskbits[j] = ~(0xffffffffU >> mask);

  if (p->src_ip.family == AF_INET) {
    addrh[0] = ntohl(p->src_ip.address.ipv4.s_addr);
    addrh[0] &= maskbits[0];
    p->src_net.address.ipv4.s_addr = htonl(addrh[0]);
    p->src_net.family = p->src_ip.family;
  }
#if defined ENABLE_IPV6
  else if (p->src_ip.family == AF_INET6) {
    memcpy(&addrh, (void *) pm_ntohl6(&p->src_ip.address.ipv6), IP6AddrSz);
    for (j = 0; j < 4; j++) addrh[j] &= maskbits[j];
    memcpy(&p->src_net.address.ipv6, (void *) pm_htonl6(addrh), IP6AddrSz);
    p->src_net.family = p->src_ip.family;
  }
#endif
}

void mask_static_src_ipaddr(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
				struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  u_int32_t addrh[4];
  u_int8_t j;

  if (p->src_ip.family == AF_INET) {
    addrh[0] = ntohl(p->src_ip.address.ipv4.s_addr);
    addrh[0] &= nt->maskbits[0]; 
    p->src_net.address.ipv4.s_addr = htonl(addrh[0]);
    p->src_net.family = p->src_ip.family;
  }
#if defined ENABLE_IPV6
  else if (p->src_ip.family == AF_INET6) {
    memcpy(&addrh, (void *) pm_ntohl6(&p->src_ip.address.ipv6), IP6AddrSz);
    for (j = 0; j < 4; j++) addrh[j] &= nt->maskbits[j]; 
    memcpy(&p->src_net.address.ipv6, (void *) pm_htonl6(addrh), IP6AddrSz);
    p->src_net.family = p->src_ip.family;
  }
#endif
}

void mask_dst_ipaddr(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  u_int32_t maskbits[4], addrh[4];
  u_int8_t j, mask;

  memset(maskbits, 0,sizeof(maskbits));
  mask = p->dst_nmask; 

  if (config.networks_no_mask_if_zero && !mask) mask = 128;

  for (j = 0; j < 4 && mask >= 32; j++, mask -= 32) maskbits[j] = 0xffffffffU;
  if (j < 4 && mask) maskbits[j] = ~(0xffffffffU >> mask);

  if (p->dst_ip.family == AF_INET) {
    addrh[0] = ntohl(p->dst_ip.address.ipv4.s_addr);
    addrh[0] &= maskbits[0];
    p->dst_net.address.ipv4.s_addr = htonl(addrh[0]);
    p->dst_net.family = p->dst_ip.family;
  }
#if defined ENABLE_IPV6
  else if (p->dst_ip.family == AF_INET6) {
    memcpy(&addrh, (void *) pm_ntohl6(&p->dst_ip.address.ipv6), IP6AddrSz);
    for (j = 0; j < 4; j++) addrh[j] &= maskbits[j];
    memcpy(&p->dst_net.address.ipv6, (void *) pm_htonl6(addrh), IP6AddrSz);
    p->dst_net.family = p->dst_ip.family;
  }
#endif
}

void mask_static_dst_ipaddr(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
				struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  u_int32_t addrh[4];
  u_int8_t j;

  if (p->dst_ip.family == AF_INET) {
    addrh[0] = ntohl(p->dst_ip.address.ipv4.s_addr);
    addrh[0] &= nt->maskbits[0];
    p->dst_net.address.ipv4.s_addr = htonl(addrh[0]);
    p->dst_net.family = p->dst_ip.family;
  }
#if defined ENABLE_IPV6
  else if (p->dst_ip.family == AF_INET6) {
    memcpy(&addrh, (void *) pm_ntohl6(&p->dst_ip.address.ipv6), IP6AddrSz);
    for (j = 0; j < 4; j++) addrh[j] &= nt->maskbits[j];
    memcpy(&p->dst_net.address.ipv6, (void *) pm_htonl6(addrh), IP6AddrSz);
    p->dst_net.family = p->dst_ip.family;
  }
#endif
}

void copy_src_mask(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  p->src_nmask = config.networks_mask;
}

void copy_dst_mask(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  p->dst_nmask = config.networks_mask;
}

void search_src_ip(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  if (p->src_ip.family == AF_INET) {
    nfd->family = AF_INET;
    nfd->entry = (u_char *) binsearch(nt, nc, &p->src_ip);
  }
#if defined ENABLE_IPV6
  else if (p->src_ip.family == AF_INET6) {
    nfd->family = AF_INET6;
    nfd->entry = (u_char *) binsearch6(nt, nc, &p->src_ip);
  }
#endif
  else {
    nfd->family = 0;
    nfd->entry = NULL;
  }
}

void search_dst_ip(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  if (p->dst_ip.family == AF_INET) {
    nfd->family = AF_INET;
    nfd->entry = (u_char *) binsearch(nt, nc, &p->dst_ip);
  }
#if defined ENABLE_IPV6
  else if (p->dst_ip.family == AF_INET6) {
    nfd->family = AF_INET6;
    nfd->entry = (u_char *) binsearch6(nt, nc, &p->dst_ip);
  }
#endif
  else {
    nfd->family = 0;
    nfd->entry = NULL;
  }
}

void search_src_host(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (!res) {
      if (config.networks_file_filter)
	p->src_ip.address.ipv4.s_addr = 0;
    }
    else {
      if (!res->net && !default_route_in_networks4_table) {
	if (config.networks_file_filter)
	  p->src_ip.address.ipv4.s_addr = 0; /* it may have been cached */
      }
    }
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (!res6) {
      if (config.networks_file_filter)
	memset(&p->src_ip.address.ipv6, 0, IP6AddrSz);
    }
    else {
      if (!res6->net[0] && !default_route_in_networks6_table) {
	if (config.networks_file_filter)
	  memset(&p->src_ip.address.ipv6, 0, IP6AddrSz); /* it may have been cached */
      }
    }
  }
#endif
}

void search_dst_host(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp,struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (!res) {
      if (config.networks_file_filter) 
	p->dst_ip.address.ipv4.s_addr = 0;
    }
    else {
      if (!res->net && !default_route_in_networks4_table) {
	if (config.networks_file_filter) 
	  p->dst_ip.address.ipv4.s_addr = 0; /* it may have been cached */
      }
    }
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (!res6) {
      if (config.networks_file_filter)
	memset(&p->dst_ip.address.ipv6, 0, IP6AddrSz);
    }
    else {
      if (!res6->net[0] && !default_route_in_networks6_table) {
	if (config.networks_file_filter)
	  memset(&p->dst_ip.address.ipv6, 0, IP6AddrSz); /* it may have been cached */
      }
    }
  }
#endif
}

#if defined ENABLE_PLABEL
void search_src_host_label(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (res && res->plabel[0] != '\0') label_to_addr(res->plabel, &p->src_ip, PREFIX_LABEL_LEN);
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (res6 && res6->plabel[0] != '\0') label_to_addr(res6->plabel, &p->src_ip, PREFIX_LABEL_LEN);
  }
#endif
}

void search_dst_host_label(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (res && res->plabel[0] != '\0') label_to_addr(res->plabel, &p->dst_ip, PREFIX_LABEL_LEN);
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (res6 && res6->plabel[0] != '\0') label_to_addr(res6->plabel, &p->dst_ip, PREFIX_LABEL_LEN);
  }
#endif
}
#endif

void search_src_nmask(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif
  u_int8_t mask = 0, default_route_in_networks_table = 0;

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    default_route_in_networks_table = default_route_in_networks4_table;
    if (!res) mask = 0;
    else mask = res->masknum;
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    default_route_in_networks_table = default_route_in_networks6_table;
    if (!res6) mask = 0;
    else mask = res6->masknum; 
  }
#endif

  if (!(config.nfacctd_net & NF_NET_FALLBACK)) {
    p->src_nmask = mask;
  }
  else {
    if (config.networks_file_no_lpm) {
      if (mask) p->src_nmask = mask;
    }
    else {
      if (mask > p->src_nmask) p->src_nmask = mask;
    }

    if (config.networks_file_filter && !mask && !default_route_in_networks_table) {
      p->src_nmask = 0;
      nfd->zero_src_nmask = TRUE;
    }
  }
}

void search_dst_nmask(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif
  u_int8_t mask = 0, default_route_in_networks_table = 0;

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    default_route_in_networks_table = default_route_in_networks4_table;
    if (!res) mask = 0;
    else mask = res->masknum;
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    default_route_in_networks_table = default_route_in_networks6_table;
    if (!res6) mask = 0;
    else mask = res6->masknum;
  }
#endif

  if (!(config.nfacctd_net & NF_NET_FALLBACK)) {
    p->dst_nmask = mask;
  }
  else {
    if (config.networks_file_no_lpm) {
      if (mask) p->dst_nmask = mask;
    }
    else {
      if (mask > p->dst_nmask) p->dst_nmask = mask;
    }

    if (config.networks_file_filter && !mask && !default_route_in_networks_table) {
      p->dst_nmask = 0;
      nfd->zero_dst_nmask = TRUE;
    }
  }
}

void search_src_as(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif
  as_t as = 0;
  u_int8_t mask = 0;

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (res) {
      as = res->as;
      mask = res->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (res6) {
      as = res6->as;
      mask = res6->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#endif

  if (!(config.nfacctd_as & NF_AS_FALLBACK)) p->src_as = as;
  else {
    if (config.networks_file_no_lpm) {
      if (mask) p->src_as = as;
    }
    else {
      if (mask >= p->src_nmask) p->src_as = as;
    }
  }
}

void search_dst_as(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif
  as_t as = 0;
  u_int8_t mask = 0;
  
  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (res) {
      as = res->as;
      mask = res->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (res6) {
      as = res6->as;
      mask = res6->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#endif

  if (!(config.nfacctd_as & NF_AS_FALLBACK)) p->dst_as = as;
  else {
    if (config.networks_file_no_lpm) {
      if (mask) p->dst_as = as;
    }
    else {
      if (mask >= p->dst_nmask) p->dst_as = as;
    }
  }
}

void search_peer_src_as(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif
  as_t as = 0;
  u_int8_t mask = 0;

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (res) {
      as = res->peer_as;
      mask = res->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (res6) {
      as = res6->peer_as;
      mask = res6->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#endif

  if (!(config.nfacctd_as & NF_AS_FALLBACK)) {
    if (pbgp) pbgp->peer_src_as = as;
  }
  else {
    if (config.networks_file_no_lpm) {
      if (mask && pbgp) pbgp->peer_src_as = as;
    }
    else {
      if (mask >= p->src_nmask) {
        if (pbgp) pbgp->peer_src_as = as;
      }
    }
  }
}

void search_peer_dst_as(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif
  as_t as = 0;
  u_int8_t mask = 0;

  if (nfd->family == AF_INET) {
    res = (struct networks_table_entry *) nfd->entry;
    if (res) {
      as = res->peer_as;
      mask = res->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#if defined ENABLE_IPV6
  else if (nfd->family == AF_INET6) {
    res6 = (struct networks6_table_entry *) nfd->entry;
    if (res6) {
      as = res6->peer_as;
      mask = res6->masknum;
    }
    else {
      as = 0;
      mask = 0;
    }
  }
#endif

  if (!(config.nfacctd_as & NF_AS_FALLBACK)) {
    if (pbgp) pbgp->peer_dst_as = as;
  }
  else {
    if (config.networks_file_no_lpm) {
      if (mask && pbgp) pbgp->peer_dst_as = as;
    }
    else {
      if (mask >= p->dst_nmask) {
        if (pbgp) pbgp->peer_dst_as = as;
      }
    }
  }
}

void search_peer_dst_ip(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  struct networks_table_entry *res = NULL;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6 = NULL;
#endif
  struct host_addr nh;
  u_int8_t mask = 0;

  if (pbgp) {
    if (nfd->family == AF_INET) {
      res = (struct networks_table_entry *) nfd->entry;
      if (res) {
        memcpy(&nh, &res->nh, sizeof(struct host_addr));
        mask = res->masknum;
      }
      else {
        memset(&nh, 0, sizeof(struct host_addr));
        mask = 0;
      }
    }
#if defined ENABLE_IPV6
    else if (nfd->family == AF_INET6) {
      res6 = (struct networks6_table_entry *) nfd->entry;
      if (res6) {
        memcpy(&nh, &res6->nh, sizeof(struct host_addr));
        mask = res6->masknum;
      }
      else {
        memset(&nh, 0, sizeof(struct host_addr));
        mask = 0;
      }
    }
#endif

    if (!(config.nfacctd_net & NF_NET_FALLBACK)) {
      memcpy(&pbgp->peer_dst_ip, &nh, sizeof(struct host_addr));
    }
    else {
      if (config.networks_file_no_lpm) {
        if (mask && pbgp) memcpy(&pbgp->peer_dst_ip, &nh, sizeof(struct host_addr));
      }
      else {
        if (mask >= p->dst_nmask) {
          if (pbgp) memcpy(&pbgp->peer_dst_ip, &nh, sizeof(struct host_addr));
	}
      }
    }
  }
}

void clear_src_host(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  memset(&p->src_ip, 0, HostAddrSz);
}

void clear_dst_host(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
			struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  memset(&p->dst_ip, 0, HostAddrSz);
}

void clear_src_net(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  memset(&p->src_net, 0, HostAddrSz);
}

void clear_dst_net(struct networks_table *nt, struct networks_cache *nc, struct pkt_primitives *p,
                        struct pkt_bgp_primitives *pbgp, struct networks_file_data *nfd)
{
  memset(&p->dst_net, 0, HostAddrSz);
}

as_t search_pretag_src_as(struct networks_table *nt, struct networks_cache *nc, struct packet_ptrs *pptrs)
{
  struct networks_table_entry *res;
  struct host_addr addr;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif

  if (pptrs->l3_proto == ETHERTYPE_IP) { 
    addr.family = AF_INET;
    addr.address.ipv4.s_addr = ((struct pm_iphdr *) pptrs->iph_ptr)->ip_src.s_addr;
    res = binsearch(nt, nc, &addr);
    if (!res) return 0;
    else return res->as;
  }
#if defined ENABLE_IPV6
  else if (pptrs->l3_proto == ETHERTYPE_IPV6) {
    addr.family = AF_INET6;
    memcpy(&addr.address.ipv6, &((struct ip6_hdr *)pptrs->iph_ptr)->ip6_src, IP6AddrSz);
    res6 = binsearch6(nt, nc, &addr);
    if (!res6) return 0;
    else return res6->as;
  }
#endif

  return 0;
}

as_t search_pretag_dst_as(struct networks_table *nt, struct networks_cache *nc, struct packet_ptrs *pptrs)
{
  struct networks_table_entry *res;
  struct host_addr addr;
#if defined ENABLE_IPV6
  struct networks6_table_entry *res6;
#endif

  if (pptrs->l3_proto == ETHERTYPE_IP) {
    addr.family = AF_INET;
    addr.address.ipv4.s_addr = ((struct pm_iphdr *) pptrs->iph_ptr)->ip_dst.s_addr;
    res = binsearch(nt, nc, &addr);
    if (!res) return 0;
    else return res->as;
  }
#if defined ENABLE_IPV6
  else if (pptrs->l3_proto == ETHERTYPE_IPV6) {
    addr.family = AF_INET6;
    memcpy(&addr.address.ipv6, &((struct ip6_hdr *)pptrs->iph_ptr)->ip6_dst, IP6AddrSz);
    res6 = binsearch6(nt, nc, &addr);
    if (!res6) return 0;
    else return res6->as;
  }
#endif

  return 0;
}

#if defined ENABLE_IPV6
void load_networks6(char *filename, struct networks_table *nt, struct networks_cache *nc)
{
  FILE *file;
  struct networks_table tmp, *tmpt = &tmp;
  struct networks_table bkt;
  struct networks_table_metadata *mdt = 0;
  char buf[SRVBUFLEN], *bufptr, *delim, *peer_as, *as, *net, *mask, *nh;
  int rows, eff_rows = 0, j, buflen, fields, prev[128];
  unsigned int index, fake_row = 0;
  u_int32_t tmpmask[4], tmpnet[4];
  struct stat st;

  /* dummy & broken on purpose */
  memset(&dummy_entry6, 0, sizeof(struct networks6_table_entry));
  dummy_entry6.masknum = 255;

  memset(&bkt, 0, sizeof(bkt));
  memset(&tmp, 0, sizeof(tmp));
  memset(&st, 0, sizeof(st));
  default_route_in_networks6_table = FALSE;

  /* backing up pre-existing table and cache */
  if (nt->num6) {
    bkt.table6 = nt->table6;
    bkt.num6 = nt->num6;
    bkt.timestamp = nt->timestamp;

    nt->table6 = 0;
    nt->num6 = 0;
    nt->timestamp = 0;
  }

  if (filename) {
    if ((file = fopen(filename,"r")) == NULL) {
      if (!(config.nfacctd_net & NF_NET_KEEP && config.nfacctd_as & NF_AS_KEEP)) {
        Log(LOG_WARNING, "WARN ( %s/%s ): [%s] file not found.\n", config.name, config.type, filename);
        return;
      }

      Log(LOG_ERR, "ERROR ( %s/%s ): [%s] file not found.\n", config.name, config.type, filename);
      exit_plugin(1);
    }
    else {
      rows = 0;
      /* 1st step: count rows for table allocation */
      while (!feof(file)) {
        if (fgets(buf, SRVBUFLEN, file) && !iscomment(buf)) rows++;
      }
      /* 2nd step: loading data into a temporary table */
      if (!freopen(filename, "r", file)) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] freopen() failed (%s).\n", config.name, config.type, filename, strerror(errno));
        goto handle_error;
      }

      /* We have no (valid) rows. We build a zeroed single-row table aimed to complete
         successfully any further lookup */
      if (!rows) {
	fake_row = TRUE;
	rows++;
      }
      
      nt->table6 = malloc(rows*sizeof(struct networks6_table_entry));
      if (!nt->table6) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Networks Table.\n", config.name, config.type, filename);
        goto handle_error;
      }

      tmpt->table6 = malloc(rows*sizeof(struct networks6_table_entry));
      if (!tmpt->table6) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Temporary Networks Table.\n", config.name, config.type, filename);
        goto handle_error;
      }

      memset(nt->table6, 0, rows*sizeof(struct networks6_table_entry));
      memset(tmpt->table6, 0, rows*sizeof(struct networks6_table_entry));
      rows = 1;

      while (!feof(file)) {
        bufptr = buf;
        memset(buf, 0, SRVBUFLEN);

        if (fgets(buf, SRVBUFLEN, file)) {
	  if (iscomment(buf)) continue;

          for (fields = 0, delim = strchr(bufptr, ','); delim; fields++) {
            bufptr = delim+1;
            delim = strchr(bufptr, ',');
          }

	  bufptr = buf;

#if defined ENABLE_PLABEL
          if (fields >= 3) {
            char *plabel, *endptr;

            delim = strchr(bufptr, ',');
            plabel = bufptr;
            *delim = '\0';
            bufptr = delim+1;
            strlcpy(tmpt->table6[eff_rows].plabel, plabel, PREFIX_LABEL_LEN);
          }
          else memset(tmpt->table6[eff_rows].plabel, 0, PREFIX_LABEL_LEN);
#else
          if (fields >= 3) {
            delim = strchr(bufptr, ',');
            *delim = '\0';
            bufptr = delim+1;
          }
#endif

          if (fields >= 2) {
            char *endptr;

            delim = strchr(bufptr, ',');
            nh = bufptr;
            *delim = '\0';
            bufptr = delim+1;
            str_to_addr(nh, &tmpt->table6[eff_rows].nh);
          }
          else memset(&tmpt->table6[eff_rows].nh, 0, sizeof(struct host_addr));

          if (fields >= 1) {
            char *endptr, *endptr2;

            peer_as = as = NULL;
            delim = strchr(bufptr, ',');
            if (delim) {
              as = bufptr;
              *delim = '\0';
              bufptr = delim+1;

              delim = strchr(as, '_');
              if (delim) {
                *delim = '\0';
                peer_as = as;
                as = delim+1;
              }
            }
            else tmpt->table6[eff_rows].peer_as = 0;

            if (as) tmpt->table6[eff_rows].as = strtoul(as, &endptr, 10);
            else tmpt->table6[eff_rows].as = 0;

            if (peer_as) tmpt->table6[eff_rows].peer_as = strtoul(peer_as, &endptr2, 10);
            else tmpt->table6[eff_rows].peer_as = 0;
          }
          else {
            tmpt->table6[eff_rows].peer_as = 0;
            tmpt->table6[eff_rows].as = 0;
          }

          if (!sanitize_buf_net(filename, bufptr, rows)) {
            delim = strchr(bufptr, '/');
            *delim = '\0';
            net = bufptr;
            mask = delim+1;

	    /* XXX: error signallation */
            if (!inet_pton(AF_INET6, net, &tmpnet)) {
	      memset(&tmpt->table6[eff_rows], 0, sizeof(struct networks6_table_entry));
	      goto cycle_end;
	    }

            buflen = strlen(mask);
            for (j = 0; j < buflen; j++) {
              if (!isdigit(mask[j])) {
                Log(LOG_ERR, "ERROR ( %s/%s ): [%s:%u] Invalid network mask '%s'.\n", config.name, config.type, filename, rows, mask);
	        memset(&tmpt->table6[eff_rows], 0, sizeof(struct networks6_table_entry));
                goto cycle_end;
              }
            }
            index = atoi(mask);
            if (index > 128) {
              Log(LOG_ERR, "ERROR ( %s/%s ): [%s:%u] Invalid network mask '%d'.\n", config.name, config.type, filename, rows, index);
	      memset(&tmpt->table6[eff_rows], 0, sizeof(struct networks6_table_entry));
              goto cycle_end;
            }

	    memset(&tmpmask, 0, sizeof(tmpmask));
            tmpt->table6[eff_rows].masknum = index;

	    for (j = 0; j < 4 && index >= 32; j++, index -= 32) tmpmask[j] = 0xffffffffU; 
	    if (j < 4 && index) tmpmask[j] = ~(0xffffffffU >> index);
            for (j = 0; j < 4; j++) tmpnet[j] = ntohl(tmpnet[j]);
	    for (j = 0; j < 4; j++) tmpnet[j] &= tmpmask[j]; /* enforcing mask on given network */ 

            memcpy(&tmpt->table6[eff_rows].net, tmpnet, IP6AddrSz);
            memcpy(&tmpt->table6[eff_rows].mask, tmpmask, IP6AddrSz);

            eff_rows++;
          }
        }

        cycle_end:
        rows++;
      }
      fclose(file);
      stat(filename, &st);

      /* We have no (valid) rows. We build a zeroed single-row table aimed to complete
         successfully any further lookup */
      if (!eff_rows) eff_rows++;

      /* 3rd step: sorting table */
      merge_sort6(filename, tmpt->table6, 0, eff_rows);
      tmpt->num6 = eff_rows;

      /* 4th step: collecting informations in the sorted table;
         we wish to handle networks-in-networks hierarchically */

      /* 4a building hierarchies */
      mdt = malloc(tmpt->num6*sizeof(struct networks_table_metadata));
      if (!mdt) {
        Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Metadata Networks Table.\n", config.name, config.type, filename);
        goto handle_error;
      }
      memset(mdt, 0, tmpt->num6*sizeof(struct networks_table_metadata));

      if (tmpt->num6) {
        for (index = 0; index < (tmpt->num6-1); index++) {
          u_int32_t net[4];
          int x, chunk;

          for (x = index+1; x < tmpt->num6; x++) {
            memcpy(&net, &tmpt->table6[x].net, IP6AddrSz);
            for (chunk = 0; chunk < 4; chunk++) net[chunk] &= tmpt->table6[index].mask[chunk];
            for (chunk = 0; chunk < 4; chunk++) {
	      if (net[chunk] == tmpt->table6[index].net[chunk]) {
                if (chunk == 3) {
		  mdt[x].level++;
		  mdt[index].childs++;
	        }
              }
              else break;
            }
          }
        }
      }

      /* 4b retrieving root entries number */
      for (index = 0, eff_rows = 0; index < tmpt->num6; index++) {
        if (mdt[index].level == 0) eff_rows++;
      }

      nt->num6 = eff_rows;
      /* 4c adjusting child counters: each parent has to know
         only the number of its directly attached childs and
         not the whole hierarchy */
      for (index = 0; index < tmpt->num6; index++) {
        int x, eff_childs = 0;

        for (x = index+1; x < tmpt->num6; x++) {
          if (mdt[index].level == mdt[x].level) break;
          else if (mdt[index].level == (mdt[x].level-1)) eff_childs++;
        }
        mdt[index].childs = eff_childs;
      }

      /* 5a step: building final networks table */
      for (index = 0; index < tmpt->num6; index++) {
        int current, next;

        if (!index) {
          current = 0; next = eff_rows;
          memset(&prev, 0, 32);
          memcpy(&nt->table6[current], &tmpt->table6[index], sizeof(struct networks6_table_entry));
        }
        else {
          if (mdt[index].level == mdt[index-1].level) current++; /* do nothing: we have only to copy our element */
          else if (mdt[index].level > mdt[index-1].level) { /* we encountered a child */
            nt->table6[current].childs_table.table6 = &nt->table6[next];
            nt->table6[current].childs_table.num6 = mdt[index-1].childs;
            prev[mdt[index-1].level] = current;
            current = next;
            next += mdt[index-1].childs;
          }
          else { /* going back to parent level */
            current = prev[mdt[index].level];
            current++;
          }
          memcpy(&nt->table6[current], &tmpt->table6[index], sizeof(struct networks6_table_entry));
        }
      }
 
      /* 5b step: debug and default route detection */
      index = 0;
      while (!fake_row && index < tmpt->num6) {
        if (config.debug) {
          int j;
          struct host_addr net_bin;
          char nh_string[INET6_ADDRSTRLEN];
          char net_string[INET6_ADDRSTRLEN];

          net_bin.family = AF_INET6;
	  memcpy(&net_bin.address.ipv6, (void *) pm_htonl6(&nt->table6[index].net), IP6AddrSz);
          addr_to_str(net_string, &net_bin);
          addr_to_str(nh_string, &nt->table6[index].nh);

          Log(LOG_DEBUG, "DEBUG ( %s/%s ): [%s] v6 nh: %s peer_asn: %u asn: %u net: %s mask: %u\n",
		config.name, config.type, filename, nh_string, nt->table6[index].peer_as,
		nt->table6[index].as, net_string, nt->table6[index].masknum); 
	}
	if (!nt->table6[index].mask[0] && !nt->table6[index].mask[1] &&
	    !nt->table6[index].mask[2] && !nt->table6[index].mask[3])
	  Log(LOG_DEBUG, "DEBUG ( %s/%s ): [%s] v6 contains a default route\n", config.name, config.type, filename);
	  default_route_in_networks6_table = TRUE;
        index++;
      }

      /* 6th step: create networks cache BUT only for the first time */
      if (!nc->cache6) {
        if (!config.networks_cache_entries) nc->num6 = NETWORKS6_CACHE_ENTRIES;
        else nc->num6 = config.networks_cache_entries;
        nc->cache6 = (struct networks6_cache_entry *) malloc(nc->num6*sizeof(struct networks6_cache_entry));
        if (!nc->cache6) {
          Log(LOG_ERR, "ERROR ( %s/%s ): [%s] malloc() failed while building Networks Cache.\n", config.name, config.type, filename);
          goto handle_error;
        }
	else Log(LOG_DEBUG, "DEBUG ( %s/%s ): [%s] IPv6 Networks Cache successfully created: %u entries.\n",
			config.name, config.type, filename, nc->num6);
      }

      /* 7th step: freeing resources */
      memset(nc->cache6, 0, nc->num6*sizeof(struct networks6_cache_entry));
      free(tmpt->table6);
      free(mdt);
      if (bkt.table6) free(bkt.table6);

      /* 8th step: setting timestamp */
      nt->timestamp = st.st_mtime;
    }
  }

  return;

  /*
     error handling: if we have a copy of the old table we will rollback it;
     otherwise we just take the exit lane. XXX: actually we are just able to
     recover malloc() troubles; efforts should be pushed in the validation of
     the new table.
  */
  handle_error:
  if (tmpt->table6) free(tmpt->table6);
  if (mdt) free(mdt);

  if (bkt.num6) {
    if (!nt->table6) {
      memcpy(nt, &bkt, sizeof(struct networks_table));
      Log(LOG_WARNING, "WARN ( %s/%s ): [%s] Rolling back the old Networks Table.\n", config.name, config.type, filename);

      /* we update the timestamp to avoid loops */
      stat(filename, &st);
      nt->timestamp = st.st_mtime;
    }
  }
  else exit_plugin(1);
}

/* sort the (sub)array v from start to end */
void merge_sort6(char * filename, struct networks6_table_entry *table, int start, int end)
{
  int middle;

  /* no elements to sort */
  if ((start == end) || (start == end-1)) return;

  /* find the middle of the array, splitting it into two subarrays */
  middle = (start+end)/2;

  /* sort the subarray from start..middle */
  merge_sort6(filename, table, start, middle);

  /* sort the subarray from middle..end */
  merge_sort6(filename, table, middle, end);

  /* merge the two sorted halves */
  merge6(filename, table, start, middle, end);
}

/*
   merge the subarray v[start..middle] with v[middle..end], placing the
   result back into v.
*/
void merge6(char *filename, struct networks6_table_entry *table, int start, int middle, int end)
{
  struct networks6_table_entry *v1, *v2;
  int v1_n, v2_n, v1_index, v2_index, i, x, s = sizeof(struct networks6_table_entry);
  int chunk;

  v1_n = middle-start;
  v2_n = end-middle;

  v1 = malloc(v1_n*s);
  v2 = malloc(v2_n*s);

  if ((!v1) || (!v2)) Log(LOG_ERR, "ERROR ( %s/%s ): [%s] memory sold out.\n", config.name, config.type, filename);

  for (i=0; i<v1_n; i++) memcpy(&v1[i], &table[start+i], s);
  for (i=0; i<v2_n; i++) memcpy(&v2[i], &table[middle+i], s);

  v1_index = 0;
  v2_index = 0;

  /* as we pick elements from one or the other to place back into the table */
  for (i=0; (v1_index < v1_n) && (v2_index < v2_n); i++) {
    /* current v1 element less than current v2 element? */
    for (chunk = 0; chunk < 4; chunk++) { 
      if (v1[v1_index].net[chunk] < v2[v2_index].net[chunk]) {
        memcpy(&table[start+i], &v1[v1_index++], s);
	break;
      }
      if (v1[v1_index].net[chunk] > v2[v2_index].net[chunk]) {
        memcpy(&table[start+i], &v2[v2_index++], s);
	break;
      }
      if (v1[v1_index].net[chunk] == v2[v2_index].net[chunk]) {
        if (chunk != 3) continue;
        else {
	  /* two network prefixes are identical; let's compare their masks */
	  for (x = 0; x < 4; x++) {
            if (v1[v1_index].mask[x] < v2[v2_index].mask[x]) {
	      memcpy(&table[start+i], &v1[v1_index++], s);
	      break;
	    }
	    if (v1[v1_index].mask[x] > v2[v2_index].mask[x]) {
              memcpy(&table[start+i], &v2[v2_index++], s);
              break;
            }
	    if (v1[v1_index].mask[x] == v2[v2_index].mask[x]) {
	      if (x != 3) continue;
	      else memcpy(&table[start+i], &v1[v1_index++], s);
	    }
	  }
	}
      }
    }
  }

  /* clean up; either v1 or v2 may have stuff left in it */
  for (; v1_index < v1_n; i++) memcpy(&table[start+i], &v1[v1_index++], s);
  for (; v2_index < v2_n; i++) memcpy(&table[start+i], &v2[v2_index++], s);

  free(v1);
  free(v2);
}

struct networks6_table_entry *binsearch6(struct networks_table *nt, struct networks_cache *nc, struct host_addr *a)
{
  int low = 0, mid, high = nt->num6-1, chunk;
  u_int32_t net[4], addrh[4], addr[4]; 
  struct networks6_table_entry *ret;

  memcpy(&addr, &a->address.ipv6, IP6AddrSz);
  memcpy(&addrh, &a->address.ipv6, IP6AddrSz);
  memcpy(&addrh, (void *) pm_ntohl6(addrh), IP6AddrSz);
  
  ret = networks_cache_search6(nc, addr);
  if (ret) {
    if (ret->masknum == 255) return NULL; /* dummy entry identification */
    else return ret;
  }

  binsearch_loop:
  while (low <= high) {
    mid = (low+high)/2;
    memcpy(&net, &addrh, IP6AddrSz); 

    for (chunk = 0; chunk < 4; chunk++) net[chunk] &= nt->table6[mid].mask[chunk];
    for (chunk = 0; chunk < 4; chunk++) {
      if (net[chunk] < nt->table6[mid].net[chunk]) {
        high = mid-1;
        goto binsearch_loop;
      }
      else if (net[chunk] > nt->table6[mid].net[chunk]) {
        low = mid+1;
        goto binsearch_loop;
      }
    }
  
    /* It's assumed we've found our element */
    if (nt->table6[mid].childs_table.table6) {
      ret = binsearch6(&nt->table6[mid].childs_table, nc, a);
      if (!ret) {
        ret = &nt->table6[mid];
        networks_cache_insert6(nc, addr, &nt->table6[mid]);
      }
    }
    else {
      ret = &nt->table6[mid];
      networks_cache_insert6(nc, addr, &nt->table6[mid]);
    }
    return ret;
  }

  networks_cache_insert6(nc, addr, &dummy_entry6);
  return NULL;
}

void networks_cache_insert6(struct networks_cache *nc, void *key, struct networks6_table_entry *result)
{
  struct networks6_cache_entry *ptr;
  unsigned int hash;
  u_int32_t *keyptr = key;
  int chunk;

  hash = networks_cache_hash6(key); 
  ptr = &nc->cache6[hash % nc->num6];
  memcpy(ptr->key, keyptr, IP6AddrSz); 
  ptr->result = result;
}

struct networks6_table_entry *networks_cache_search6(struct networks_cache *nc, void *key)
{
  struct networks6_cache_entry *ptr;
  unsigned int hash;
  u_int32_t *keyptr = key;
  int chunk;

  hash = networks_cache_hash6(key);
  ptr = &nc->cache6[hash % nc->num6];
  for (chunk = 0; chunk < 4; chunk++) {
    if (ptr->key[chunk] == keyptr[chunk]) continue;
    else return NULL;
  } 

  return ptr->result;
}

unsigned int networks_cache_hash6(void *key)
{
  u_int32_t a, b, c;
  u_int32_t *keyptr = (u_int32_t *)key;

  a = keyptr[0];
  b = keyptr[1];
  c = keyptr[2];

  a += JHASH_GOLDEN_RATIO;
  b += JHASH_GOLDEN_RATIO;
  c += 140281; /* trivial hash rnd */
  __jhash_mix(a, b, c);

  return c;
}
#endif


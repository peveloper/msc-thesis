/* adudump.c
 * Extract ADUs (application data units) from TCP traffic and print them.
 *
 * Usage example: adudump -Csort trace1 trace2 ...
 *
 * Where trace is a dag or pcap trace of TCP traffic.  For example, on mexico:
 *
 * dagsnap -d/dev/dag0 -s 3600 -o trace
 *
 * This code is largely based on Don Smith's DAGfilter.c code.
 *
 * Note: understanding this code depends on a thorough understanding of TCP.
 *
 * TODO:
 * - Make a pass at cleaning up the logic.
 * - Organize file better.  (Split into separate files?)
 * - There's gotta be a cleaner way to write binary!  (With write(), perhaps?)
 * - Shortcut state establishment; log ADUs without waiting for SYNs (diff.
 *   record type).  Or, at least give an indication of how much running traffic
 *   is not being analyzed.
 * - Generalize to UDP traffic.
 * - Maybe use Coral "interval" API (w/ switch?) to instead print summary info.
 *
 * Jeff Terrell, 2006-11-08
 */

/*
 * This code is architecturally divided into five layers:
 *
 * 1. The data interface - packetHandler().  CoralReef is used for this.
 * 2. The data structures - segment, flow, connection.  This should be
 *    independent of the data format.
 * 3. The data structure interface - synOnly(), init(), lookupConn(), etc.
 *    This should be independent of the data format and separate from the
 *    conn/flow logic.
 * 4. The conn/flow logic - processSegment(), handleOpenConn(), etc.  This
 *    should use the data structure interface but be independent of the data
 *    structures themselves.  (XXX - this is a long way from not using layer 2
 *    stuff...e.g. flo->adu_seqno = ___, etc.)
 * 5. User interface routines - option-handling, output, etc.
 *
 * The layout of the code will be divided into these layers as much as
 * possible.
 */

#include "adudump.h"


/********************************************************
 *                     Layer 1 code                     *
 ********************************************************/

uint32_t segments_read;

void
setupSources()
{
  int ret;
  struct timeval interval;
  ret = coral_open_all();
  if (ret == 0) {
    error("opened 0 sources");
  } else if (ret < 0) {
    error("error opening sources");
  }
  coral_start_all();
  if (coral_add_pcap_ipfilter("tcp") == -1) {
    perror("coral_add_pcap_ipfilter(\"tcp\")");
  }
  if (cfg.interval_secs) {
    if (coral_get_interval(&interval) != 0) {
      error("error with coral_get_interval()");
    }
    if (coral_read_pkt_init(NULL, NULL, &interval) == -1) {
      error("error with coral_read_pkt_init(NULL, NULL, interval)");
    }
  } else {
    if (coral_read_pkt_init(NULL, NULL, NULL) == -1) {
      error("error with coral_read_pkt_init(NULL, NULL, NULL)");
    }
  }
}

void
closeSources()
{
  if (coral_stop_all() == -1) {
    error("error stopping sources");
  }
  if (coral_close_all() == -1) {
    error("error closing sources");
  }
}

int
isIncomingIface(coral_iface_t* iface)
{
  if (cfg.in_iface[ coral_interface_get_number(iface) ]) {
    return 1;
  } else {
    return 0;
  }
}

int
isMirroredIface(coral_iface_t* iface)
{
  if (cfg.mirrored_iface[ coral_interface_get_number(iface) ]) {
    return 1;
  } else {
    return 0;
  }
}

int
isLocalNet(struct in_addr *address)
{
  for (int i=0; i<cfg.num_innets; i++) {
    if (ntohl(address->s_addr) == ntohl(cfg.innets[i])) {
      return 1;
    }
  }
  return 0;
}

void
packetHandler(coral_iface_t *iface, coral_pkt_result_t *pkt_result)
{
  coral_pkt_buffer_t ip_buf;
  coral_pkt_buffer_t tcp_buf;
  struct sniff_ip *ipHeader;
  struct sniff_tcp *tcpHeader;
  segment seg;
  int offset;
  int local = 0;

  CORAL_TIMESTAMP_TO_TIMEVAL(iface, pkt_result->timestamp, &(seg.ts));
  coral_get_payload(pkt_result->packet, &ip_buf);
  coral_get_payload(&ip_buf, &tcp_buf);
  ipHeader = (struct sniff_ip*)(ip_buf.buf);
  tcpHeader = (struct sniff_tcp*)(tcp_buf.buf);

  if (ip_buf.protocol != CORAL_NETPROTO_IP) {
    if (ip_buf.protocol == CORAL_NETPROTO_IPv6) {
      pkts.ipv6++;
    } else {
      pkts.nonip++;
    }
    return;
  }
  if (tcp_buf.protocol != CORAL_IPPROTO_TCP) {
    if (tcp_buf.protocol == CORAL_IPPROTO_UDP) {
      pkts.udp++;
    } else if (tcp_buf.protocol == CORAL_IPPROTO_ICMP) {
      pkts.icmp++;
    } else {
      pkts.other_ip++;
    }
    return;
  }

  // ensure we have the first fragment
  offset = ntohs(ipHeader->ip_off);
  if ((offset & 0x1fff) != 0) {
    pkts.frag++;
    return;
  }

  // Is the packet inbound or outbound?  That is, which address (if any) is
  // local?
  if (isIncomingIface(iface)) {
    // if this packet's iface has been specified as inbound, or if one of the
    // addresses has been specified as local, then we know which address is the
    // local one.
    local = 1; // i.e. dst address is local
  } else if (isLocalNet(&(ipHeader->ip_dst))) {
    local = 1;
  } else if (isLocalNet(&(ipHeader->ip_src))) {
    local = 2; // i.e. src address is local
  } else if (isMirroredIface(iface)) {
    // if this packet's iface has been specified as mirrored, then 'local' has
    // no meaning, and we arbitrarily specify the lesser of the two addresses
    // to be 'local'.  We do this so that, for any given pair of addresses, we
    // have a consistent way of ordering them.  The consistency is key, because
    // the state that we keep per connection must be accessed in a manner
    // independent of a packet's direction.
    if (ntohl(ipHeader->ip_dst.s_addr) < ntohl(ipHeader->ip_src.s_addr)) {
      local = 1;
    } else {
      local = 2;
    }
    //local = (ipHeader->ip_dst.s_addr < ipHeader->ip_src.s_addr ? 1 : 2);
  } else if (cfg.num_ifaces > 0) {
    // if this packet's iface is neither inbound nor mirrored, and if
    // inbound ifaces have been specified, then we assume the packet is
    // outbound, so the source address is the local address.
    local = 2;
  } else {
    // otherwise, we do not assign either address as local, and we discard the
    // packet, because either:
    // (a) we defined locality by network and neither address matched a local
    // network, or
    // (b) we specified a mirrored iface (but no inbound iface) and the current
    // iface is not mirrored.
    pkts.no_local++;
    return;
  }

  if (local == 1) {
    // OK, I *think* the reason it's correct not to have ntohl() around the
    // addresses is that we're doing an inet_ntoa when printing.
    seg.loc_addr.s_addr = ipHeader->ip_dst.s_addr;
    seg.rem_addr.s_addr = ipHeader->ip_src.s_addr;
    seg.loc_port = ntohs(tcpHeader->dest);
    seg.rem_port = ntohs(tcpHeader->source);
    seg.dir = 1;
  } else {
    seg.loc_addr.s_addr = ipHeader->ip_src.s_addr;
    seg.rem_addr.s_addr = ipHeader->ip_dst.s_addr;
    seg.loc_port = ntohs(tcpHeader->source);
    seg.rem_port = ntohs(tcpHeader->dest);
    seg.dir = 0;
  }

  seg.flags = 0;
  if (tcpHeader->fin) {
    seg.flags |= TH_FIN;
  }
  if (tcpHeader->syn) {
    seg.flags |= TH_SYN;
  }
  if (tcpHeader->ack) {
    seg.flags |= TH_ACK;
  }
  if (tcpHeader->rst) {
    seg.flags |= TH_RST;
  }

  if (cfg.inbound_only && seg.flags == TH_SYN && seg.dir == 0 &&
      !isMirroredIface(iface)) {
    // This is an outbound SYN segment, so we ignore this segment and thus the
    // entire connection.  We only do this for non-mirrored interfaces.
    pkts.outbound_syn++;
    return;
  }

  if (cfg.port_whitelist && seg.flags == TH_SYN && ! cfg.ports[ ntohs(tcpHeader->dest) ]) {
    // We are only observing certain destination ports.  Ignore this SYN
    // segment (and thus the entire connection) if this destination port is not
    // in the list.
    pkts.not_whitelisted_port++;
    return;
  }

  seg.start_seqno = ntohl(tcpHeader->seq);
  seg.ackno = ntohl(tcpHeader->ack_seq);
  seg.payload = ntohs(ipHeader->ip_len) - (tcpHeader->doff << 2) - sizeof(struct sniff_ip);
  seg.next_seqno = seg.start_seqno + seg.payload;
  seg.iface = coral_interface_get_number(iface);

  pkts.processed_tcp++;
  processSegment(&seg);
}

int
asciiPacketHandler()
{
  segment seg;
  char line[300], buf1[40], buf2[40], buf3[40];
  uint8_t a,b,c,d;
  char *linep = &(line[0]), *buf1p = &(buf1[0]), *buf2p = &(buf2[0]), *buf3p = &(buf3[0]);


  if (!fgets(line, 300, cfg.ascii_input)) {
    pkts.ascii_input_failure++;
    return 0;
  }


  // the timestamp
  buf1p = strsep(&linep, " ");
  printf("%s\n", buf1p);
  buf2p = strsep(&buf1p, ".");
  printf("%s\n", buf2p);
  seg.ts.tv_sec = strtoul(buf2p, NULL, 10);
  seg.ts.tv_usec = strtoul(buf1p, NULL, 10);



  printf("%s\n", linep);
  // the first IP.port pair
  buf3p = strsep(&linep, " ");
  printf("%s\n", linep);
  buf1p = strsep(&linep, " ");
  printf("%s\n", buf1p);
  buf2p = strsep(&buf1p, ".");
  printf("%s\n", buf2p);
  a = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  b = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  printf("DIO3\n");
  c = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  d = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  seg.loc_port = strtoul(buf2p, NULL, 10);
  seg.loc_addr.s_addr = htonl((uint32_t)( a << 24 | b << 16 | c << 8 | d ));



  buf1p = strsep(&linep, " ");
  if (buf1p[0] == cfg.ascii_iface) {
    seg.dir = 1;
  } else {
    seg.dir = 0;
  }



  // the second IP.port pair
  buf1p = strsep(&linep, " ");
  buf2p = strsep(&buf1p, ".");
  a = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  b = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  c = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  d = strtoul(buf2p, NULL, 10);
  buf2p = strsep(&buf1p, ".");
  buf1p = strsep(&buf2p, ":");
  // converting address back to network order because we're using inet_ntoa().
  //

  seg.rem_port = strtoul(buf1p, NULL, 10);
  seg.rem_addr.s_addr = htonl((uint32_t)( a << 24 | b << 16 | c << 8 | d ));



  // the flags: '.' for none, 'S' for SYN, 'F' for FIN, 'FP' for FIN|PSH, etc.
  // we'll set ACK later.
  buf1p = strsep(&linep, " ");
  seg.flags = 0;
  for (int i=0; i<strlen(buf1p); i++) {
    if (buf1p[i] == '.') {
      // no-op...we already set it to 0
    } else if (buf1p[i] == 'S') {
      seg.flags |= TH_SYN;
    } else if (buf1p[i] == 'R') {
      seg.flags |= TH_RST;
    } else if (buf1p[i] == 'F') {
      seg.flags |= TH_FIN;
    }
  }

  // the sequence number, if applicable.  Otherwise, the ack #.
  buf1p = strsep(&linep, " ");
  seg.start_seqno = 0;
  seg.next_seqno = 0;
  seg.payload = 0;
  if (buf1p[0] != 'a') {
    buf2p = strsep(&buf1p, ":");
    seg.start_seqno = strtoull(buf2p, NULL, 10);
    buf2p = strsep(&buf1p, "(");
    seg.next_seqno = strtoull(buf2p, NULL, 10);
    buf2p = strsep(&buf1p, ")");
    seg.payload = strtoul(buf2p, NULL, 10);
    buf1p = strsep(&linep, " ");
  }



  // the ack #.
  if (buf1p[0] == 'a') {
    buf1p = strsep(&linep, " ");
    seg.ackno = strtoull(buf1p, NULL, 10);
    seg.flags |= TH_ACK;
  } else {
    seg.ackno = 0;
  }

  if (cfg.inbound_only && seg.flags == TH_SYN && seg.dir == 0) {
    // This is an outbound SYN segment, so we ignore this segment and thus the
    // entire connection.
    pkts.outbound_syn++;
    return 1;
  }

  if (cfg.port_whitelist && seg.flags == TH_SYN && ! cfg.ports[ (seg.dir==0 ? seg.rem_port : seg.loc_port) ]) {
    // We are only observing certain destination ports.  Ignore this SYN
    // segment (and thus the entire connection) if this destination port is not
    // in the list.
    pkts.not_whitelisted_port++;
    return 1;
  }

  processSegment(&seg);
  return 1;
}


/********************************************************
 *                     Layer 2 code                     *
 ********************************************************/

// return the bucket index for the given connection
unsigned int
hashConnection(unsigned long addr1, unsigned long addr2, unsigned int port1, unsigned int port2)
{
  hash_key h;
  h.IPAddr1.s_addr = addr1;
  h.IPAddr2.s_addr = addr2;
  h.Port1 = port1;
  h.Port2 = port2;
  return (hashlittle(&h, sizeof(hash_key), 0)) % NUM_BUCKETS;
}

// returns b - a
struct timeval
elapsedTime(struct timeval *a, struct timeval *b)
{
  struct timeval c;
  int usecs = b->tv_usec - a->tv_usec;
  if (usecs < 0) {
    c.tv_usec = usecs + 1e6;
    c.tv_sec = b->tv_sec - a->tv_sec - 1;
  } else {
    c.tv_usec = usecs;
    c.tv_sec = b->tv_sec - a->tv_sec;
  }
  return c;
}

double
elapsedMS(struct timeval *a, struct timeval *b)
{
  // XXX - OK, this is totally screwy.  I can't delete the first 2 lines, or
  // else this function returns NaN.  Probably some sort of weird memory
  // bug...need to run through valgrind, but I'll leave it like this for now.
  float h;
  h = (float)(((float)((float)(b->tv_sec) - (float)(a->tv_sec)) + (float)((float)((float)(b->tv_usec) - (float)(a->tv_usec)) * (float)(0.000001)))*(float)(1000.0));
  return (float)((b->tv_sec - a->tv_sec) + (b->tv_usec - a->tv_usec) * 0.000001) * 1000.0;
}


/********************************************************
 *                     Layer 3 code                     *
 ********************************************************/

#define SEQ_GT(a,b) ((int)((a)-(b)) > 0)
#define SEQ_LT(a,b) ((int)((a)-(b)) < 0)
#define synOnly(c) (((c) & TH_SYN) && !((c) & TH_ACK))
#define synAck(c) (((c) & TH_SYN) && ((c) & TH_ACK))
#define ack(c) ((c) & TH_ACK)
#define rst(c) ((c) & TH_RST)
#define fin(c) ((c) & TH_FIN)

// Lookup the connection corresponding to this segment.  If appropriate, add it
// to the connection table if it isn't there.
connection*
lookupConn(segment *seg, connection **table)
{
  connection *curr;
  unsigned int bucket;

  bucket = hashConnection(seg->loc_addr.s_addr, seg->rem_addr.s_addr, seg->loc_port, seg->rem_port);

  curr = &(table[bucket][0]);
  while (curr) {
    if (curr->use_state && seg->loc_addr.s_addr == curr->IPAddr1.s_addr && seg->rem_addr.s_addr == curr->IPAddr2.s_addr && seg->loc_port == curr->Port1 && seg->rem_port == curr->Port2) {
      break;
    }
    curr = curr->next_in_bucket;
  }

  if (curr) {
    // The connection was found.
    return curr;
  }
  return NULL;
}

// Establish state to track the current connection.
connection*
createConn(segment *seg, connection **table)
{
  connection *prev, *curr;
  struct timeval seg_ts = seg->ts;
  uint8_t dir = seg->dir;
  unsigned int bucket = hashConnection(seg->loc_addr.s_addr, seg->rem_addr.s_addr, seg->loc_port, seg->rem_port);

  // Now, create a new entry for this connection.  Reuse a stale or
  // already-allocated entry if possible.
  curr = &(table[bucket][0]);
  while (curr) {
    int ts_valid = curr->flows[dir].last_send.tv_sec != 0 ||
                   curr->flows[1-dir].last_send.tv_sec != 0;
    int idle = maxIdleTimeout(&(curr->flows[1-dir].last_send), &(seg_ts)) &&
               maxIdleTimeout(&(curr->flows[dir].last_send), &(seg_ts));
    int idle_timeout = ts_valid && idle;
    int same_key = curr->IPAddr1.s_addr == seg->loc_addr.s_addr &&
                   curr->IPAddr2.s_addr == seg->rem_addr.s_addr &&
                   curr->Port1 == seg->loc_port &&
                   curr->Port2 == seg->rem_port;
    int reuse_entry = !(curr->use_state) || idle_timeout || same_key;

    if (reuse_entry) {
      int i;
      if (curr->use_state) {
        for (i=0; i<2; i++) {
          if (curr->flows[i].state == ACTIVE) {
            printAduRecord(NULL, curr, i, &seg->ts, 1);
          }
        }
      }

      curr->use_state = 1;
      updateTime(&(curr->flows[dir]), &seg_ts);
      curr->IPAddr1.s_addr = seg->loc_addr.s_addr;
      curr->IPAddr2.s_addr = seg->rem_addr.s_addr;
      curr->Port1 = seg->loc_port;
      curr->Port2 = seg->rem_port;
      if (seg->flags == TH_SYN) {
        curr->state = NEW_SYN;
      } else if ((seg->flags & TH_SYN) != 0) {
        curr->state = NEW_SYNACK;
      } else {
        curr->state = PARTIAL;
      }

      for (i=0; i<2; i++) {
        curr->flows[i].state = INACTIVE;
        curr->flows[i].init_seqno = 0;
        curr->flows[i].next_seqno = 0;
        curr->flows[i].init_ackno = 0;
        curr->flows[i].high_ackno = 0;
        curr->flows[i].last_send.tv_sec = 0;
        curr->flows[i].last_send.tv_usec = 0;
        curr->flows[i].adu_seqno = 0;
        curr->flows[i].iface = 0;
      }
      return curr;
    }
    prev = curr;
    curr = curr->next_in_bucket;
  }

  // Can't reuse a stale entry, so allocate a new entry & chain it into the
  // bucket.
  prev->next_in_bucket = (connection*) malloc(sizeof(connection));
  if (prev->next_in_bucket == NULL) {
    error("problem allocating memory for new connection on input line %d\n", segments_read);
  }
  curr = prev->next_in_bucket;
  curr->use_state = 1;
  updateTime(&(curr->flows[dir]), &seg_ts);
  curr->next_in_bucket = NULL;
  curr->flows[0].state = INACTIVE;
  curr->flows[1].state = INACTIVE;
  if (seg->flags == TH_SYN) {
    curr->state = NEW_SYN;
  } else if ((seg->flags & TH_SYN) != 0) {
    curr->state = NEW_SYNACK;
  } else {
    curr->state = PARTIAL;
  }
  return curr;
}

// initialize connection table data and other structures
void
init(connection **conns, connection **closed_conns)
{
  int i,j;
  for (i=0; i<NUM_BUCKETS; i++) {
    conns[i] = (connection*) malloc(BUCKET_SIZE * sizeof(connection));
    if (conns[i] == NULL) {
      error("failed to allocate memory for bucket %d\n", i);
    }
    closed_conns[i] = (connection*) malloc(BUCKET_SIZE * sizeof(connection));
    if (closed_conns[i] == NULL) {
      error("failed to allocate memory for bucket %d\n", i);
    }
    for (j=0; j<BUCKET_SIZE; j++) {
      resetConn(&(conns[i][j]));
    }
    for (j=0; j<BUCKET_SIZE; j++) {
      resetConn(&(closed_conns[i][j]));
    }
    conns[i][0].next_in_bucket = &(conns[i][1]);
    conns[i][1].next_in_bucket = &(conns[i][2]);
    conns[i][2].next_in_bucket = NULL;
    closed_conns[i][0].next_in_bucket = &(closed_conns[i][1]);
    closed_conns[i][1].next_in_bucket = &(closed_conns[i][2]);
    closed_conns[i][2].next_in_bucket = NULL;
  }
  pkts.nonip = 0;
  pkts.ipv6 = 0;
  pkts.udp = 0;
  pkts.icmp = 0;
  pkts.other_ip = 0;
  pkts.frag = 0;
  pkts.no_local = 0;
  pkts.outbound_syn = 0;
  pkts.not_whitelisted_port = 0;
  pkts.ascii_input_failure = 0;
  pkts.processed_tcp = 0;
  pkts.missed_start = 0;
  pkts.lacked_ack = 0;
}

void
freeBucket(connection *bucket)
{
  connection *next;
  connection *cur = bucket[2].next_in_bucket;
  int j,k;

  for (k=0; k<3; k++) {
    if (bucket[k].use_state == 0) {
      continue;
    }
    for (j=0; j<2; j++) {
      if (bucket[k].flows[j].state == ACTIVE &&
          bucket[k].flows[j].next_seqno != bucket[k].flows[j].adu_seqno) {
        printIncRecord(&(bucket[k]), j);
      }
    }
  }

  while (cur != NULL) {
    for (j=0; j<2; j++) {
      if (cur->flows[j].state == ACTIVE) {
        printIncRecord(cur, j);
      }
    }
    next = cur->next_in_bucket;
    free(cur);
    cur = next;
  }
  free(bucket);
}

void
cleanup(connection **conns)
{
  int i;
  for (i=0; i<NUM_BUCKETS; i++) {
    freeBucket(conns[i]);
    freeBucket(closed_conns[i]);
  }
}

void
storageStats(connection** table, storstat* stats)
{
  int bucket, i, j;
  connection *cur;
  int prealloc_unused=0, prealloc_used=0, alloc_unused=0, alloc_used=0;
  int chain_length[NUM_BUCKETS];
  int num_used[NUM_BUCKETS];
  int tot_chain_len=0;

  for (bucket=0; bucket<NUM_BUCKETS; bucket++) {
    cur = &(table[bucket][0]);
    chain_length[bucket] = 0;
    i=0;
    j=0;
    while (cur != NULL) {
      i++;
      if (i <= 3) {
        if (cur->use_state) {
          prealloc_used++;
          chain_length[bucket] = i;
          j++;
        } else {
          prealloc_unused++;
        }
      } else {
        chain_length[bucket] = i;
        if (cur->use_state) {
          alloc_used++;
          j++;
        } else {
          alloc_unused++;
        }
      }
      cur = cur->next_in_bucket;
    }
    num_used[bucket] = j;
    tot_chain_len += chain_length[bucket];
  }

  stats->prealloc_used = prealloc_used;
  stats->prealloc_unused = prealloc_unused;
  stats->alloc_used = alloc_used;
  stats->alloc_unused = alloc_unused;
  stats->avg_chain = (float)tot_chain_len / (float)NUM_BUCKETS;
  stats->avg_usage = (float)(prealloc_used + alloc_used) / (float)NUM_BUCKETS;

  stats->long_chain = 0;
  for (i=0; i<10; i++) {
    stats->length_freq[i] = 0;
    stats->used_freq[i] = 0;
  }

  for (bucket=0; bucket<NUM_BUCKETS; bucket++) {
    if (chain_length[bucket] > stats->long_chain) {
      stats->long_chain = chain_length[bucket];
    }
    if (chain_length[bucket] < 9) {
      stats->length_freq[chain_length[bucket]] += 1;
    } else {
      stats->length_freq[9] += 1;
    }
    if (num_used[bucket] < 9) {
      stats->used_freq[num_used[bucket]] += 1;
    } else {
      stats->used_freq[9] += 1;
    }
  }
}

unsigned long long
aduSize(flow *flow)
{
  long long accum = 0;
  accum = flow->next_seqno - flow->adu_seqno;
  if (accum < 0) {
    accum += (unsigned long) 0xffffffff;
  }
  return (unsigned long long) accum;
}

// New connection started with same connectionID: reset state & restart.
void
reuseConn(segment *seg, connection *conn)
{
  conn->flows[0].last_send.tv_sec = 0;
  conn->flows[1].last_send.tv_sec = 0;
  // XXX - might should dump out active ADUs here?  I dunno...judgement call...
  conn->flows[0].state = INACTIVE;
  conn->flows[1].state = INACTIVE;
}

void
endConn(segment *seg, connection *conn)
{
  connection *this_conn;
  conn->use_state = 0;
  printEndRecord(&seg->ts, conn);
  this_conn = createConn(seg, closed_conns);
  // need to validate the timestamp so that we can reuse stale entries
  this_conn->flows[0].last_send.tv_sec = seg->ts.tv_sec;
  this_conn->flows[0].last_send.tv_usec = seg->ts.tv_usec;
  this_conn->flows[1].last_send.tv_sec = seg->ts.tv_sec;
  this_conn->flows[1].last_send.tv_usec = seg->ts.tv_usec;
}

void
resetConn(connection *conn)
{
  int i;
  conn->use_state = 0;
  conn->state = NEW_SYN;
  conn->acpt_dir = -1;
  conn->ord_major = 0;
  conn->ord_minor = 0;
  for (i=0; i<2; i++) {
    conn->flows[i].state = INACTIVE;
    conn->flows[i].init_seqno = 0;
    conn->flows[i].next_seqno = 0;
    conn->flows[i].init_ackno = 0;
    conn->flows[i].high_ackno = 0;
    conn->flows[i].iface = 0;
  }
}

int
maxIdleTimeout(struct timeval *a, struct timeval *b)
{
  struct timeval c, d;
  c = elapsedTime(a, b);
  // cfg.max_idle is in seconds
  d.tv_usec = 0;
  d.tv_sec = cfg.max_idle;
  return timevalGreaterThan(&c, &d);
}

// returns (a > b)
int
timevalGreaterThan(struct timeval *a, struct timeval *b)
{
  if (a->tv_sec > b->tv_sec) {
    return 1;
  }
  if (a->tv_sec < b->tv_sec) {
    return 0;
  }
  return (a->tv_usec > b->tv_usec);
}

void
updateTime(flow* flow, struct timeval *ts)
{
  flow->last_send.tv_sec = ts->tv_sec;
  flow->last_send.tv_usec = ts->tv_usec;
}

void
cleanExit(connection **conns)
{
  if (cfg.report) {
    printIfaceStats();
  }
  if (!cfg.ascii) {
    closeSources();
  }
  cleanup(conns);
  free(conns);
  if (cfg.report) {
    printPacketStats();
  }
  exit(0);
}


/********************************************************
 *                     Layer 4 code                     *
 ********************************************************/

void
processSegment(segment *seg)
{
  connection *this_conn = NULL;

  // Lookup the connection for this segment.
  // If this is a SYN segment, add it to the connection table if it isn't there.
  if ((this_conn = lookupConn(seg, conns)) == NULL) {
    // Connection not already in table.  Establish state to track conn, if this
    // segment has enough state.
    if (startTrackingConnNow(seg)) {
      this_conn = createConn(seg, conns);
    } else {
      pkts.missed_start++;
      return;
    }
  }

  // The "normal" progression for a connection is NEW_SYN (initially), then
  // HALF_OPEN (after observing a SYN), then SEQUENTIAL (after observing a
  // SYN-ACK), then, optionally, CONCURRENT (if both flows are sending
  // simultaneously).  Not all flows will be marked CONCURRENT.
  switch (this_conn->state) {
    case NEW_SYN:
      handleNewConn(seg, this_conn); break;
    case NEW_SYNACK:
      handleNewSynAckConn(seg, this_conn); break;
    case HALF_OPEN:
      handleHalfOpenConn(seg, this_conn); break;
    case PARTIAL:
      handlePartialConn(seg, this_conn); break;
    case MOSTLY_OPEN:
      handleMostlyOpenConn(seg, this_conn); break;
    case SEQUENTIAL:
    case CONCURRENT:
      handleOpenConn(seg, this_conn); break;
    case UNUSED: break;
  }
}

// When the segment is a SYN, and the connection is brand-new, initialize the
// connection.  We can be assured that the segment is a SYN-only segment
// because this function is only called when the connection's state is NEW_SYN,
// which only happens upon observation of the SYN-only segment.
void
handleNewConn(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  conn->init_dir = dir;
  conn->ord_major = 0;
  conn->ord_minor = 0;
  flo->init_seqno = seg->start_seqno;
  flo->next_seqno = seg->next_seqno + 1;
  flo->adu_seqno = seg->next_seqno;
  flo->last_send.tv_sec = seg->ts.tv_sec;
  flo->last_send.tv_usec = seg->ts.tv_usec;
  flo->iface = seg->iface + 1;
  conn->state = HALF_OPEN;
  printSynRecord(&seg->ts, conn, dir);
}

// I think this can only happen when the first segment seen for a connection is
// the SYN-ACK.
void
handleNewSynAckConn(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];
  conn->init_dir = 1 - dir;
  conn->ord_major = 0;
  conn->ord_minor = 0;
  rflo->init_seqno = seg->ackno;
  rflo->next_seqno = seg->ackno;
  rflo->init_ackno = seg->start_seqno;
  rflo->high_ackno = seg->next_seqno;
  rflo->adu_seqno = rflo->init_seqno;
  rflo->last_send.tv_sec = seg->ts.tv_sec;
  rflo->last_send.tv_usec = seg->ts.tv_usec;

  conn->acpt_dir = dir;
  flo->init_seqno = seg->start_seqno;
  flo->next_seqno = seg->start_seqno + 1;
  flo->adu_seqno = seg->start_seqno;
  flo->init_ackno = seg->ackno;
  flo->high_ackno = seg->ackno;
  flo->last_send.tv_sec = seg->ts.tv_sec;
  flo->last_send.tv_usec = seg->ts.tv_usec;
  flo->iface = seg->iface + 1;
  conn->state = MOSTLY_OPEN;
  printSynAckRecord(&seg->ts, conn, dir);
}

// When we've already observed a SYN, but not yet the SYN-ACK.
void
handleHalfOpenConn(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];

  if (synAck(seg->flags))
  {
    flo->iface = seg->iface + 1;
    if (dir != conn->init_dir && conn->acpt_dir == -1) {
      printRttRecord(&seg->ts, conn, dir);
      conn->acpt_dir = 0;
    }
    if (dir == conn->init_dir) {
      // This is nonsensical.  The SYN-ACK's direction is the same as the SYN's.
      endConn(seg, conn);
      return;
    }

    conn->acpt_dir = dir;
    conn->state = MOSTLY_OPEN;
    flo->init_seqno = seg->start_seqno;
    flo->next_seqno = seg->start_seqno + 1;
    flo->adu_seqno = seg->start_seqno;
    flo->init_ackno = seg->ackno;
    flo->high_ackno = seg->ackno;
    // The acceptor (B) conveys its understanding of the initiator's (A's)
    // initial seqno.  When A sent multiple SYN's with different seqno's, this
    // might be different than the state we've saved.  So, update it
    // unconditionally.
    rflo->init_seqno = seg->ackno - 1;
    rflo->next_seqno = seg->ackno;
    rflo->adu_seqno = seg->ackno - 1;

  } else if (rst(seg->flags) || fin(seg->flags)) {
    endConn(seg, conn);
  } else if (synOnly(seg->flags) && dir == conn->init_dir) {
    // the SYN is being retransmitted
    flo->last_send.tv_sec = seg->ts.tv_sec;
    flo->last_send.tv_usec = seg->ts.tv_usec;
  }
}

// We've seen the first two parts of the three way handshake, and we're waiting
// on the third before tracking the connection in earnest.  This only gets
// called when conn->state == MOSTLY_OPEN.
void
handleMostlyOpenConn(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];

  if (synOnly(seg->flags) && dir == conn->init_dir) {
    flo->init_seqno = seg->start_seqno;
    flo->next_seqno = seg->next_seqno + 1;
    flo->adu_seqno = seg->next_seqno;
    flo->last_send.tv_sec = seg->ts.tv_sec;
    flo->last_send.tv_usec = seg->ts.tv_usec;
    flo->iface = seg->iface + 1;
    return;
  }
  if (seg->payload != 0 || dir != conn->init_dir || flo->high_ackno != 0 ||
      synAck(seg->flags) || fin(seg->flags) || rst(seg->flags)) {
    return;
  }

  // This segment is the ACK of the SYN-ACK, i.e. the 3rd part of the 3-way
  // handshake.  The connection is now started.
  printSeqRecord(&seg->ts, conn, dir);
  conn->state = SEQUENTIAL;
  flo->high_ackno = seg->ackno;
  // This segment tells the connection initiator (A) what the acceptor (B)
  // thinks A's initial seqno is.  When the SYN-ACK is sent multiple times
  // with different seqno's, the current flow state will not agree.  So,
  // update the current flow state.
  rflo->init_seqno = seg->ackno - 1;
  rflo->next_seqno = seg->ackno;
  rflo->adu_seqno = seg->ackno - 1;
  flo->init_ackno = seg->ackno;

  // Tricky things happen when the SYN or SYN-ACK was lost.  For example, say
  // client sends SYN, server sends SYN-ACK with ISN=X, but the SYN-ACK was
  // lost.  Client re-sends SYN, server responds with a SYN-ACK with ISN=Y.
  // We must get the ISN right, or else we will later conclude that the
  // server has sent an ADU of some random size between 0-4GB.
  // This is our clue: the ackno of this ACK (the 3rd part of the 3-way
  // handshake) is the client telling the server which SYN-ACK it's
  // responding to.  Therefore:
  conn->flows[1-dir].init_seqno = seg->ackno;
  conn->flows[1-dir].next_seqno = seg->ackno;
  if (flo->iface == 0) {
    flo->iface = seg->iface + 1;
  }
}

// We missed the SYN for this connection, but we're gonna start tracking the
// connection here, having received a data packet.  Note that the inferences we
// can make at this point are weaker.  We won't know:
// - The total size of the current ADU (because it might have already started).
// - The major ordinals for the entire connection.
// - The minor ordinals for the current sequence.
void
handlePartialConn(segment *seg, connection *conn)
{
  uint8_t seg_dir = seg->dir;
  conn->use_state = 1;
  conn->state = SEQUENTIAL;
  conn->init_dir = -1;
  conn->acpt_dir = -1;
  conn->ord_major = -1;
  conn->ord_minor = -1;

  conn->flows[seg_dir].state = ACTIVE;
  conn->flows[seg_dir].init_seqno = seg->start_seqno;
  conn->flows[seg_dir].next_seqno = seg->next_seqno;
  conn->flows[seg_dir].init_ackno = seg->ackno;
  conn->flows[seg_dir].high_ackno = seg->ackno;
  conn->flows[seg_dir].last_send.tv_sec = seg->ts.tv_sec;
  conn->flows[seg_dir].last_send.tv_usec = seg->ts.tv_usec;
  conn->flows[seg_dir].adu_seqno = seg->start_seqno;
  conn->flows[seg_dir].iface = seg->iface + 1;

  conn->flows[1-seg_dir].state = INACTIVE;
  conn->flows[1-seg_dir].init_seqno = seg->ackno;
  conn->flows[1-seg_dir].next_seqno = seg->ackno;
  conn->flows[1-seg_dir].init_ackno = seg->start_seqno;
  conn->flows[1-seg_dir].high_ackno = seg->next_seqno;
  conn->flows[1-seg_dir].last_send.tv_sec = seg->ts.tv_sec;
  conn->flows[1-seg_dir].last_send.tv_usec = seg->ts.tv_usec;
  conn->flows[1-seg_dir].adu_seqno = 0;
  printPartRecord(&seg->ts, conn, seg_dir);
}

// The latest segment is from a sequential or concurrent connection
void
handleOpenConn(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];

  if (synOnly(seg->flags) &&
      (seg->start_seqno != flo->init_seqno || flo->state == CLOSED)) {
    // New connection started with same connectionID: reset state & restart.
    reuseConn(seg, conn);
    conn->state = HALF_OPEN;
    conn->init_dir = dir;
    flo->init_seqno = seg->start_seqno;
    flo->next_seqno = seg->start_seqno + 1;
    flo->iface = seg->iface + 1;
    return;
  } else if (synAck(seg->flags) &&
      seg->start_seqno != flo->init_seqno && flo->state != ACTIVE &&
      rflo->state != ACTIVE && conn->ord_major <= 0) {
    // A re-sent its SYN segment with the same seqno, and the above case didn't
    // catch it, so the connection was still marked as OPEN.  Now, B sent a
    // second SYN/ACK segment, but with a different seqno.
    // Note the condition that flo->state != ACTIVE.  I've seen cases in which
    // a host re-sends a SYN-ACK just after transmitting an ADU.  If we reset
    // the next_seqno in that case, the ADU's size will be way off (usually
    // around 4GB in size) when it is printed.
    flo->init_seqno = seg->start_seqno;
    flo->next_seqno = seg->start_seqno + 1;
    flo->adu_seqno = seg->start_seqno;
    flo->iface = seg->iface + 1;
  }

  if (flo->state == CLOSED) {
    // No further updates should occur to a CLOSED flow.
    return;
  }

  if (rst(seg->flags)) {
    handleOpenConnRst(seg, conn);
    return;
  }

  if (fin(seg->flags)) {
    handleOpenConnFin(seg, conn);
    return;
  }

  // All segments other than the initial SYN should have the ACK flag set.  We
  // already know that this segment doesn't have the SYN set, as that would
  // have been handled either in the first condition of this function or in
  // handleNewConnection().  So, this segment is a violation of TCP standards,
  // or else an error, and we ignore it.
  if (!ack(seg->flags)) {
    pkts.lacked_ack++;
    return;
  }

  if (conn->ord_major < 0 && SEQ_LT(seg->ackno, rflo->init_seqno) &&
      rflo->init_seqno - seg->ackno < 3*1460) {
    // We earlier started tracking a connection because we saw a data segment
    // (even though we missed the SYN).  We used the segment's init_seqno as
    // the start of the ADU.  Now, we see an ACK in the opposite direction with
    // a lower ackno than the initial seqno, which indicates that there was
    // more to the ADU than we first thought.  So, we report the unseen ADU.
    rflo->next_seqno = rflo->init_seqno;
    rflo->init_seqno = seg->ackno;
    rflo->adu_seqno = seg->ackno;
    printAduRecord(&seg->ts, conn, 1-dir, &seg->ts, 1);
    flo->init_ackno = seg->ackno;
  }

  // We want to ignore pure-ACK segments (i.e. ACKs without any payload).
  // There are two cases:
  // 1. SEQUENTIAL connection
  //    There is only one direction sending at this point, and the pure-ACKs in
  //    the other direction only confirm that the data reached the destination.
  //    Since we care about ADUs, we only care about how much data the
  //    application attempts to send, not whether it was transmitted
  //    successfully.
  // 2. CONCURRENT connection
  //    Both directions are sending data, so seeing a pure-ACK means one of
  //    them has stopped sending.  In this case, the data segments from the
  //    still-sending direction will have all the information we need to
  //    reconstruct the application-level behavior.
  // UPDATE: there is 1 exception, which is the case just above.
  if (seg->payload == 0) {
    return;
  }

  if (conn->state == SEQUENTIAL) {
    switch (flo->state) {
      case INACTIVE:
        handleSeqInactiveFlow(seg, conn);
        break;
      case ACTIVE:
        handleSeqActiveFlow(seg, conn);
        break;
      case CLOSED:
        // Do nothing...wait for connection to end.
        // We might be seeing an out-of-order segment here.
        break;
    }
  } else {
    if (SEQ_GT(seg->next_seqno, flo->next_seqno)) {
      checkQuietTime(seg, conn);
      updateTime(flo, &seg->ts);
      // 9*1460 was insufficient, and I want to be plenty safe.  This is
      // unfortunately an imperfect heuristic that will sometimes be too low
      // and sometimes be too high.  However, 15*1460 is 21900, which means
      // that the odds of a random seqno (between 0 and 2^32) being within
      // this range is 1 in nearly 200,000.
      if (SEQNO_ABS_DIFF(flo->next_seqno, seg->next_seqno) <= 15*1460) {
        flo->next_seqno = seg->next_seqno; 
      }
      flo->high_ackno = seg->ackno;
    }
  }
}

// Handle the case in which the segment belongs to an inactive flow in a
// sequential connection.
void
handleSeqInactiveFlow(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];
  // Was there a gap between the highest seqno seen in this flow and the

  // current segment's start_seqno?
  int gap = SEQ_GT(seg->start_seqno, flo->next_seqno);
  // Is concurrency detected on this segment?
  int conc = SEQ_GT(rflo->next_seqno, seg->ackno) &&
             SEQ_GT(seg->next_seqno, rflo->high_ackno);
  // No new data.
  int full_retrans = !SEQ_GT(seg->next_seqno,  flo->next_seqno);
  // Some old data, but also some new data.
  int part_retrans = SEQ_GT(flo->next_seqno, seg->start_seqno) &&
                     SEQ_GT(seg->next_seqno, flo->next_seqno);
  int partial_conn = conn->ord_major < 0;

  if (full_retrans && !partial_conn) {
    updateTime(flo, &seg->ts);
    return;
  }

  if (!partial_conn) {
    // OK, this is tricky, and I'm not sure this is the right way to handle
    // it...  There is a possibility that this segment is actually
    // out-of-order, and the real beginning-of-ADU segment is still en route.
    if (flo->next_seqno == rflo->high_ackno && gap) {
      if (SEQNO_ABS_DIFF(rflo->high_ackno, seg->start_seqno) > 5*1460) {
        // I've seen a case where the hosts didn't actually agree on one of the
        // seqno's.  One host thought it was X, and the other host thought it
        // was Y.  The result is that the ADU was about the same size as X-Y
        // (about 1 GB in that case), even though it should have been 344
        // bytes.
        // So, if the gap is bigger than a few MSS's, then start the current
        // ADU at the segment's start_seqno, ignoring the flow's idea of what
        // the next_seqno should be.
        flo->adu_seqno = seg->start_seqno;
      } else {
        flo->adu_seqno = rflo->high_ackno; // actual ADU begin?
      }
    } else {
      // This segment is the beginning segment of a new ADU and not OoO.
      flo->adu_seqno = guessADUStart(conn, seg);
    }
    flo->state = ACTIVE;
    flo->next_seqno = seg->next_seqno;
    flo->high_ackno = seg->ackno;
    updateTime(flo, &seg->ts);

    // If the reverse flow is inactive, we don't need to check for concurrency.
    if (rflo->state == INACTIVE) {
      return;
    }
    // Test for a concurrent connection.
    if (SEQ_GT(rflo->next_seqno, seg->ackno) &&
        SEQ_GT(seg->next_seqno, rflo->high_ackno)) {
      // Concurrent connection found.  The idea is that there are un-ACK'd
      // segments in both flows of the connection.
      conn->state = CONCURRENT;
      translateConcOrdinals(conn);
      printConcRecord(&seg->ts, conn);
    } else if (rflo->state == ACTIVE) {
      // Still a sequential connection, but the current segment marks the end of
      // the reverse ADU.
      printAduRecord(&seg->ts, conn, 1-dir, &seg->ts, 1);
      rflo->state = INACTIVE;
    }

  } else {
    // Note that the full_retrans, part_retrans, gap, and in-order conditions
    // are (a) mutually exclusive and (b) exhaustive.  conc/!conc is an
    // orthogonal dichotomy.
    if (full_retrans) {
      flo->next_seqno = seg->next_seqno;
      flo->init_seqno = seg->start_seqno;
      flo->adu_seqno = seg->start_seqno;
      printAduRecord(&seg->ts, conn, dir, &seg->ts, 1);
    } else if (part_retrans) {
      flo->next_seqno = flo->init_seqno;
      flo->init_seqno = seg->start_seqno;
      flo->adu_seqno = seg->start_seqno;
      printAduRecord(&seg->ts, conn, dir, &seg->ts, 1);
      flo->adu_seqno = flo->next_seqno;
      flo->next_seqno = seg->next_seqno;
    } else {
      flo->state = ACTIVE;
      flo->adu_seqno = flo->next_seqno;
      flo->next_seqno = seg->next_seqno;
      flo->high_ackno = seg->ackno;
    }
    if (conc) {
      conn->state = CONCURRENT;
      translateConcOrdinals(conn);
      printConcRecord(&seg->ts, conn);
    }
  }
}

// Handle the case in which the segment belongs to an active flow in a
// sequential connection.
void
handleSeqActiveFlow(segment *seg, connection *conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];
  // Is concurrency detected on this segment?
  int conc = SEQ_GT(rflo->next_seqno, seg->ackno) &&
             SEQ_GT(seg->next_seqno, rflo->high_ackno);
  int partial_conn = conn->ord_major < 0;

  if (!SEQ_GT(seg->next_seqno, flo->next_seqno)) {
    // Retransmitted segment, so it affects neither flow.
    updateTime(flo, &seg->ts);
    return;
  }

  // with the advent of partial connection tracking, it is now possible for
  // concurrency to be detected here.
  if (partial_conn && conc) {
    conn->state = CONCURRENT;
    translateConcOrdinals(conn);
    printConcRecord(&seg->ts, conn);
  }

  // else, new and in-order segment, so update flow state.
  if (flo->next_seqno <= seg->start_seqno ||
      (SEQ_LT(seg->start_seqno, flo->next_seqno) &&
       SEQ_GT(seg->next_seqno, flo->next_seqno))) {
    // I struggled with whether to make the first conjunct above a == or <=.
    // <= checks the quiet time even when the segment is OoO, which I
    // eventually decided to do, for no particularly good reason.
    // The 2nd conjunct covers the case in which a segment contains some repeat
    // data and some new data--in other words, a segment that crosses a
    // previously established segment boundary.  This is unusual, but it does
    // happen.
    checkQuietTime(seg, conn);
  }
  if (SEQNO_ABS_DIFF(flo->next_seqno, seg->next_seqno) <= 15*1460) {
    flo->next_seqno = seg->next_seqno;
  }
  flo->high_ackno = seg->ackno;
  updateTime(flo, &seg->ts);
}

// In an open connection, handle the arrival of a FIN segment
void
handleOpenConnFin(segment* seg, connection* conn)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];
  // Was there a gap between the highest seqno seen in this flow and the
  // current segment's start_seqno?
  int gap = SEQ_GT(seg->start_seqno, flo->next_seqno);
  // Is concurrency detected on this segment?
  int conc = (SEQ_GT(rflo->next_seqno, seg->ackno) &&
              SEQ_GT(seg->next_seqno, rflo->high_ackno));

  // The concurrent case is easier because, no matter what this flow does, it
  // won't affect the reverse flow.  So, we handle this case first.
  if (conn->state == CONCURRENT) {
    if (aduIdleTimeout(&flo->last_send, &seg->ts) &&
        SEQ_GT(seg->next_seqno, flo->next_seqno)) {
      printAduRecord(&seg->ts, conn, dir, &seg->ts, 1);
      flo->adu_seqno = guessADUStart(conn, seg);
    }
    flo->high_ackno = seg->ackno;
    if (SEQ_GT(seg->next_seqno, flo->next_seqno) &&
        SEQNO_ABS_DIFF(flo->next_seqno, seg->next_seqno) <= 15*1460) {
      flo->next_seqno = seg->next_seqno;
    }
    if (flo->next_seqno != flo->adu_seqno) {
      printAduRecord(NULL, conn, dir, &seg->ts, 1);
    }
    flo->state = CLOSED;
    if (rflo->state == CLOSED) {
      endConn(seg, conn);
    }
    return;
  }

  // else, the connection is sequential (so far, although we might detect
  // concurrency on this segment)
  //
  // It's a little easier to handle the case in which the reverse flow is not
  // active (i.e. either INACTIVE or CLOSED), because we don't need to worry
  // about ending and reporting the active ADU in the reverse direction (or the
  // associated concurrency checks).  The cases of the reverse flow's state
  // (INACTIVE or CLOSED) are handled almost the same, except that if it's
  // closed, we will subsequently close the connection as well.
  if (rflo->state != ACTIVE) {
    if (flo->state == ACTIVE) {
      // XXX: if payload, concurrency check here?
      if ((gap || seg->payload > 0) &&
          aduIdleTimeout(&flo->last_send, &seg->ts)) {
        // The active ADU in the forward direction should be split because an
        // idle timeout was detected.
        printAduRecord(&seg->ts, conn, dir, &seg->ts, 2);
        flo->adu_seqno = guessADUStart(conn, seg);
      }
      if (SEQNO_ABS_DIFF(flo->next_seqno, seg->next_seqno) <= 3*1460 &&
          SEQ_GT(seg->next_seqno, flo->next_seqno)) {
        // Don't update the flow's next_seqno if the segment's seqno is pretty
        // unlikely.  I've seen FINs with seemingly arbitrary seqno's; without
        // this condition, this update could cause very large (false) ADUs to
        // be printed.
        flo->next_seqno = seg->next_seqno;
      }
      // We print this knowing that no more data will be added to this ADU
      // because of the FIN promise (see below).
      printAduRecord(NULL, conn, dir, &seg->ts, 1);
    } else if (flo->state == INACTIVE) {
      if (gap || seg->payload > 0) {
        // The FIN betrays that the host sent more data.
        if (conc) {
          conn->state = CONCURRENT;
          translateConcOrdinals(conn);
          printConcRecord(&seg->ts, conn);
        }
        if (SEQNO_ABS_DIFF(flo->next_seqno, seg->next_seqno) <= 3*1460) {
          flo->adu_seqno = guessADUStart(conn, seg);
          flo->next_seqno = seg->next_seqno;
          printAduRecord(NULL, conn, dir, &seg->ts, 1);
        }
      }
    }
    flo->state = CLOSED;
    if (rflo->state == CLOSED) {
      endConn(seg, conn);
    }
    return;
  }

  // else, rflo->state == ACTIVE, which implies flo->state == INACTIVE (because
  // the connection is sequential so far)
  //
  // The order is very important here.  If concurrency is detected on this
  // segment, the CONC record should be reported before reporting any ADU or
  // END records.  Then, if we're still sequential, report the 'til-now-active
  // ADU in the reverse direction.  Then, report the ADU in the forward
  // direction, if applicable.
  if (conc) {
    conn->state = CONCURRENT;
    translateConcOrdinals(conn);
    printConcRecord(&seg->ts, conn);
  } else if (gap || seg->payload > 0) {
    printAduRecord(&seg->ts, conn, 1-dir, &seg->ts, 1);
    rflo->state = INACTIVE;
  }
  if (gap || seg->payload > 0) {
    // The FIN segment indicates the host intended to send data, either because
    // there is a gap in the seqno's or because the FIN segment contains
    // payload.  We can go ahead and report the ADU because we know that it is
    // over; it will not continue after the FIN segment, because the FIN
    // constitutes a promise by the host that it will not send more data.
    flo->adu_seqno = guessADUStart(conn, seg);
    flo->high_ackno = seg->ackno;
    flo->next_seqno = seg->next_seqno;
    printAduRecord(NULL, conn, dir, &seg->ts, 1);
  }
  flo->state = CLOSED;
  // We don't need to check the state of the reverse flow, because we already
  // know it isn't closed.  If it was, we would have returned before getting to
  // this point.
}

// The sender of the RST guarantees that no further [new] data will be sent.
void
handleOpenConnRst(segment* seg, connection* conn)
{
  uint8_t dir = seg->dir;
  // XXX - what about wrapping issues?
  int32_t delta = seg->start_seqno - conn->flows[dir].next_seqno;
  flow *flo = &conn->flows[dir];
  flow *rflo = &conn->flows[1-dir];

  if (delta < 0) {
    // This does actually happen sometimes...
    return;
  } else if (delta == 0 || delta == 1) {
    // All the data sent in this direction has been accounted for.
    // delta == 1 usually means the RST is the next logical segment of this
    // flow.  Of course, sometimes it could imply a gap of 1 byte, but I hope
    // that is a rare case.
    if (flo->state == ACTIVE) {

      if (seg->payload &&
          !aduIdleTimeout(&flo->last_send, &seg->ts)) {
        flo->next_seqno = seg->next_seqno;
      } else if (seg->payload &&
          aduIdleTimeout(&flo->last_send, &seg->ts)) {
        printAduRecord(&seg->ts, conn, dir, &seg->ts, 2);
        flo->adu_seqno = guessADUStart(conn, seg);
        flo->next_seqno = seg->next_seqno;
      }
      printAduRecord(NULL, conn, dir, &seg->ts, 1);

    } else if (flo->state == INACTIVE && seg->payload) {
      if (rflo->state == ACTIVE) {
        // XXX - concurrency check here?
        printAduRecord(&seg->ts, conn, 1-dir, &seg->ts, 1);
        rflo->state = INACTIVE;
      }
      flo->adu_seqno = guessADUStart(conn, seg);
      flo->next_seqno = seg->next_seqno;
      printAduRecord(NULL, conn, dir, &seg->ts, 1);
    }

    flo->state = CLOSED;
    if (rflo->state == CLOSED) {
      endConn(seg, conn);
    }
    return;
  }

  // else, delta > 1:
  //
  // The RST packet is out-of-order: its seqno is greater than the highest
  // seqno seen.  Therefore, there is some data that the RST sender
  // intended to send, but that we haven't yet seen.  However, we can now
  // infer the intent of the RST sender.  We also know that it won't send
  // more data (because RST implies this), so we can report the size of the
  // entire ADU sent as the size of the "gap", i.e. delta.
  if (delta > 3*1460) {
    // Some TCP implementations will sometimes send a RST with a random
    // seqno.  So, don't trust the current gap if it seems unreasonable.
    if (flo->state == ACTIVE) {
      printAduRecord(NULL, conn, dir, &seg->ts, 1);
    }
    flo->state = CLOSED;
    if (rflo->state == CLOSED) {
      endConn(seg, conn);
    }
    return;
  }

  if (flo->state == INACTIVE) {
    flo->adu_seqno = flo->next_seqno;
    flo->next_seqno = seg->next_seqno;
  }
  if (ack(seg->flags) && seg->ackno < rflo->next_seqno) {
    conn->state = CONCURRENT;
    translateConcOrdinals(conn);
    printConcRecord(&seg->ts, conn);
  } else if (rflo->state == ACTIVE) {
    printAduRecord(NULL, conn, 1-dir, &seg->ts, 1);
    rflo->state = INACTIVE;
  }
  flo->next_seqno = seg->start_seqno;
  printAduRecord(NULL, conn, dir, &seg->ts, 1);
  flo->state = CLOSED;
  if (rflo->state == CLOSED) {
    endConn(seg, conn);
  }
  return;
}

// Is this segment a good time to start tracking this connection?
int
startTrackingConnNow(segment* seg)
{
  if (lookupConn(seg, closed_conns) != NULL && !synOnly(seg->flags)) {
    // If a connection with this ID has been closed already, we never start
    // tracking on the same ID without seeing a SYN.  The reason for this is
    // that connection closes are often sloppy, given that they often operate
    // in a lossy environment.  This makes me less tolerant of packet drops
    // (i.e. if the SYN is dropped, too bad), but hey, I never claimed to be
    // very robust w.r.t. packet drops anyway.
    return 0;
  }
  // Inbound-only has special significance here.
  if (cfg.inbound_only) {
    if (seg->flags == TH_SYN) {
      return (seg->dir == 1);
    }
    if (seg->flags == (TH_SYN | TH_ACK)) {
      return (seg->dir == 0);
    }
    // If we're only tracking inbound connections and we miss the SYN, then
    // we'll never know whether a connection is inbound or not.  So, don't
    // track it.
    return 0;
  }

  // SYN is the obvious place to start tracking a connection.  We can even know
  // the initial sequence numbers.
  if ((seg->flags & TH_SYN) != 0) {
    return 1;
  }

  // However, as long as there's a payload, we should be able to start tracking
  // here.
  if (seg->payload != 0 && !cfg.inbound_only && cfg.track_partials) {
    return 1;
  }
  return 0;
}

unsigned long
guessADUStart(connection *conn, segment *seg)
{
  uint8_t dir = seg->dir;
  flow *flo = &conn->flows[dir];
  if (flo->next_seqno != 0) {
    return flo->next_seqno;
  }
  if (flo->init_seqno != 0) {
    return flo->init_seqno;
  }
  return seg->start_seqno;
}

void
checkQuietTime(segment *seg, connection *conn)
{
  flow *flo = &conn->flows[seg->dir];
  // If the flow has been quiet for a while, one of two things happened:
  // 
  // 1. The sending application has not been sending data, or
  // 2. Some segment (perhaps an ACK) was lost, and the sender timed out.
  // 
  // Case 2 is detected by an retransmitted segment.  Note that we don't care
  // about case 2 because it is a transport-level effect, and it says nothing
  // about the application's behavior.  On the other hand, case 1 means the
  // current ADU should be ended.  (We assume this function isn't called when
  // case 2 is possible.)
  if (aduIdleTimeout(&flo->last_send, &(seg->ts))) {
    printAduRecord(&seg->ts, conn, seg->dir, &seg->ts, 2);
    flo->adu_seqno = guessADUStart(conn, seg);
  }
}

int
aduIdleTimeout(struct timeval *a, struct timeval *b)
{
  struct timeval c, d;
  if (a->tv_sec == 0) {
    return 0;
  }
  c = elapsedTime(a, b);
  // cfg.quiet_time is in milliseconds
  d.tv_usec = (cfg.quiet_time * 1000) % 1000000;
  d.tv_sec = (cfg.quiet_time / 1000);
  return timevalGreaterThan(&c, &d);
}

// when a connection becomes concurrent, ord_major counts # of ADUs in init.
// direction and ord_minor counts # of ADUs in other direction.  This function
// makes the transition.
void
translateConcOrdinals(connection* conn)
{
  int newmaj, newmin;

  if (conn->ord_major == -1) {
    // Connection started out as partial, so don't track ordinals.
    return;
  }
  if (conn->ord_major % 2 == 0) {
    // current direction is same as initial direction
    newmaj = (conn->ord_major >> 1) + conn->ord_minor;
    newmin = (conn->ord_major >> 1);
  } else {
    newmaj = (conn->ord_major + 1) >> 1;
    newmin = newmaj - 1 + conn->ord_minor;
  }

  if (conn->init_dir == 0) {
    conn->ord_major = newmaj;
    conn->ord_minor = newmin;
  } else {
    conn->ord_major = newmin;
    conn->ord_minor = newmaj;
  }
}

void
updateOrdinals(connection* conn, int flow_dir, int do_ordinal)
{
  if (conn->ord_major == -1) {
    // Don't track ordinals for connections that started out as partials.
    return;
  }
  if (conn->state == SEQUENTIAL) {
    if (do_ordinal == 1) {
      conn->ord_major += 1;
      conn->ord_minor = 0;
    } else if (do_ordinal == 2) {
      conn->ord_minor += 1;
    }
  } else if (conn->state == CONCURRENT) {
    if (flow_dir == 0) {
      conn->ord_major += 1;
    } else {
      conn->ord_minor += 1;
    }
  }
}


/********************************************************
 *                     Layer 5 code                     *
 ********************************************************/

void
error(char *fmt, ...) 
{
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  exit(1);
}

void
printTextHelper(struct timeval *ts, connection *conn, char *leader, char dir, int iface, char *trailer)
{
  fprintf(cfg.out, "%s ", leader);
  if (ts != NULL) {
    fprintf(cfg.out, "%ld.%06ld ", ts->tv_sec, ts->tv_usec);
  } else {
    fprintf(cfg.out, "X.X ");
  }
  fprintf(cfg.out, "%s.%u %c%u ", inet_ntoa(conn->IPAddr1), conn->Port1, dir, iface);
  // combining these statements means IPAddr1 is printed twice
  fprintf(cfg.out, "%s.%u%s\n", inet_ntoa(conn->IPAddr2), conn->Port2, trailer);
}

#define PRINT_TS(t) \
  (char)(((t).tv_sec & 0xff000000) >> 24), \
  (char)(((t).tv_sec & 0x00ff0000) >> 16), \
  (char)(((t).tv_sec & 0x0000ff00) >> 8), \
  (char)(((t).tv_sec & 0x000000ff)), \
  (char)(((t).tv_usec & 0xff000000) >> 24), \
  (char)(((t).tv_usec & 0x00ff0000) >> 16), \
  (char)(((t).tv_usec & 0x0000ff00) >> 8), \
  (char)(((t).tv_usec & 0x000000ff))

#define PRINT_IP_PORT(i,p) \
  ((i) & 0xff000000) >> 24, \
  ((i) & 0x00ff0000) >> 16, \
  ((i) & 0x0000ff00) >> 8, \
  ((i) & 0x000000ff), \
  ((p) & 0x0000ff00) >> 8, \
  ((p) & 0x000000ff)

void
printBinaryHelper(struct timeval *ts, connection *conn, char type, uint8_t dir)
{
  if (ts != NULL) {
    fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c", type, PRINT_TS((*ts)));
  } else {
    fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c", type, 0, 0, 0, 0, 0, 0, 0, 0);
  }
  fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c%c%c%c%c",
      PRINT_IP_PORT(ntohl(conn->IPAddr1.s_addr), conn->Port1), dir,
      PRINT_IP_PORT(ntohl(conn->IPAddr2.s_addr), conn->Port2));
}

void
printSynRecord(struct timeval *ts, connection *conn, uint8_t dir)
{
  if (!cfg.binary) {
    printTextHelper(ts, conn, "SYN:", (dir == 0 ? '>' : '<'), conn->flows[dir].iface, "");
  } else {
    printBinaryHelper(ts, conn, 1, (dir == 0 ? 1 : 2));
    fprintf(cfg.out, "%c", 0);
  }
}

void
printSynAckRecord(struct timeval *ts, connection *conn, uint8_t dir)
{
  if (!cfg.binary) {
    printTextHelper(ts, conn, "SAK:", (dir == 0 ? '>' : '<'), conn->flows[dir].iface, "");
  } else {
    printBinaryHelper(ts, conn, 8, (dir == 0 ? 1 : 2));
    fprintf(cfg.out, "%c", 0);
  }
}

void
printRttRecord(struct timeval *ts, connection *conn, uint8_t dir)
{
  flow *rflo = &conn->flows[1-dir];
  if (!cfg.binary) {
    char buf[40];
    sprintf(buf, " %.06f", elapsedMS(&rflo->last_send, ts) / 1000);
    printTextHelper(ts, conn, "RTT:", (dir == 0 ? '>' : '<'), conn->flows[dir].iface, buf);
  } else {
    struct timeval elapsed =
      elapsedTime(&rflo->last_send, ts);
    printBinaryHelper(ts, conn, 2, (dir == 0 ? 1 : 2));
    fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c", PRINT_TS(elapsed), 0);
  }
}

void
printSeqRecord(struct timeval *ts, connection *conn, uint8_t dir)
{
  if (!cfg.binary) {
    printTextHelper(ts, conn, "SEQ:", (dir == 0 ? '>' : '<'), conn->flows[dir].iface, "");
  } else {
    printBinaryHelper(ts, conn, 3, (dir == 0 ? 1 : 2));
    fprintf(cfg.out, "%c", 0);
  }
}

void
printConcRecord(struct timeval *ts, connection *conn)
{
  if (!cfg.binary) {
    printTextHelper(ts, conn, "CNC:", 'x', 0, "");
  } else {
    printBinaryHelper(ts, conn, 4, 3);
    fprintf(cfg.out, "%c", 0);
  }
}

void
printPartRecord(struct timeval *ts, connection *conn, uint8_t dir)
{
  if (!cfg.binary) {
    printTextHelper(ts, conn, "PART:", (dir == 0 ? '>' : '<'), conn->flows[dir].iface, "");
  } else {
    printBinaryHelper(ts, conn, 9, (dir == 0 ? 1 : 2));
    fprintf(cfg.out, "%c", 0);
  }
}

void
printEndRecord(struct timeval *ts, connection *conn)
{
  if (!cfg.binary) {
    printTextHelper(ts, conn, "END:", 'x', 0, "");
  } else {
    printBinaryHelper(ts, conn, 6, 3);
    fprintf(cfg.out, "%c", 0);
  }
}

void
printIncRecord(connection *conn, int flow_dir)
{
  if (!cfg.binary) {
    char buf[40];
    sprintf(buf, " %llu %s",
        aduSize(&(conn->flows[flow_dir])),
        (conn->state == SEQUENTIAL ? "SEQ" : "CONC"));
    printTextHelper(NULL, conn, "INC:", (flow_dir == 0 ? '>' : '<' ), conn->flows[flow_dir].iface, buf);
  } else {
    unsigned long long ltmp;
    printBinaryHelper(NULL, conn, 7, flow_dir+1);
    ltmp = aduSize(&(conn->flows[flow_dir]));
    // I tried doing this within the printf, but it failed for some reason. :-(
    unsigned long long ctmp[8];
    ctmp[0] = (((ltmp) & 0xff00000000000000ULL) >> 56);
    ctmp[1] = (((ltmp) & 0x00ff000000000000ULL) >> 48);
    ctmp[2] = (((ltmp) & 0x0000ff0000000000ULL) >> 40);
    ctmp[3] = (((ltmp) & 0x000000ff00000000ULL) >> 32);
    ctmp[4] = (((ltmp) & 0x00000000ff000000ULL) >> 24);
    ctmp[5] = (((ltmp) & 0x0000000000ff0000ULL) >> 16);
    ctmp[6] = (((ltmp) & 0x000000000000ff00ULL) >> 8);
    ctmp[7] = (((ltmp) & 0x00000000000000ffULL));
    fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c%c",
        (char)(ctmp[0]), (char)(ctmp[1]), (char)(ctmp[2]), (char)(ctmp[3]),
        (char)(ctmp[4]), (char)(ctmp[5]), (char)(ctmp[6]), (char)(ctmp[7]),
        (conn->state == SEQUENTIAL ? 3 : 4), 0);
  }
}

void
printAduRecord(struct timeval *elapsed_ts, connection *conn, int flow_dir, struct timeval *seg_ts, uint8_t do_ordinal)
{
  if (!cfg.binary) {
    char buf[40];
    if (elapsed_ts != NULL) {
      if (conn->ord_minor != -1) {
        sprintf(buf, " %llu %s %.6f",
            aduSize(&(conn->flows[flow_dir])),
            (conn->state == SEQUENTIAL ? "SEQ" : "CONC"),
            elapsedMS(&(conn->flows[flow_dir].last_send), elapsed_ts)/1000);
      } else {
        // Same as above, but add a "+" to indicate that the ADU is *at least*
        // this large; we don't know because we might not have seen the
        // beginning of the ADU.
        sprintf(buf, " +%llu %s %.6f",
            aduSize(&(conn->flows[flow_dir])),
            (conn->state == SEQUENTIAL ? "SEQ" : "CONC"),
            elapsedMS(&(conn->flows[flow_dir].last_send), elapsed_ts)/1000);
        conn->ord_minor = -2;
      }
    } else {
      sprintf(buf, " %llu %s",
          aduSize(&(conn->flows[flow_dir])),
          (conn->state == SEQUENTIAL ? "SEQ" : "CONC"));
    }

    if (cfg.ordinals) {
      if (conn->state == SEQUENTIAL) {
        if (conn->ord_major == -1) {
          sprintf(buf, "%s ?.?", buf);
        } else {
          sprintf(buf, "%s %d.%d", buf, conn->ord_major, conn->ord_minor);
        }
      } else if (conn->state == CONCURRENT) {
        if (flow_dir == 0) {
          if (conn->ord_major == -1) {
            sprintf(buf, "%s 0.X", buf);
          } else {
            sprintf(buf, "%s 0.%d", buf, conn->ord_major);
          }
        } else {
          if (conn->ord_major == -1) {
            sprintf(buf, "%s 1.X", buf);
          } else {
            sprintf(buf, "%s 1.%d", buf, conn->ord_minor);
          }
        }
      }
    }
    updateOrdinals(conn, flow_dir, do_ordinal);
    printTextHelper(seg_ts, conn, "ADU:", (flow_dir==0 ? '>' : '<'), conn->flows[flow_dir].iface, buf);

  } else {
    unsigned long long ltmp;
    printBinaryHelper(seg_ts, conn, 5, flow_dir+1);
    ltmp = aduSize(&(conn->flows[flow_dir]));

    // I tried doing this within the printf, but it failed for some reason. :-(
    unsigned long long ctmp[8];
    ctmp[0] = (((ltmp) & 0xff00000000000000ULL) >> 56);
    ctmp[1] = (((ltmp) & 0x00ff000000000000ULL) >> 48);
    ctmp[2] = (((ltmp) & 0x0000ff0000000000ULL) >> 40);
    ctmp[3] = (((ltmp) & 0x000000ff00000000ULL) >> 32);
    ctmp[4] = (((ltmp) & 0x00000000ff000000ULL) >> 24);
    ctmp[5] = (((ltmp) & 0x0000000000ff0000ULL) >> 16);
    ctmp[6] = (((ltmp) & 0x000000000000ff00ULL) >> 8);
    ctmp[7] = (((ltmp) & 0x00000000000000ffULL));

    fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c",
        (char)(ctmp[0]), (char)(ctmp[1]), (char)(ctmp[2]), (char)(ctmp[3]),
        (char)(ctmp[4]), (char)(ctmp[5]), (char)(ctmp[6]), (char)(ctmp[7]),
        (conn->state == SEQUENTIAL ? 3 : 4));
    if (elapsed_ts != NULL) {
      struct timeval elapsed =
        elapsedTime(&(conn->flows[flow_dir].last_send), elapsed_ts);
      fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c",
          PRINT_TS(elapsed), 0);
    } else {
      fprintf(cfg.out, "%c%c%c%c%c%c%c%c%c", 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
  }
}

void
printPacketStats()
{
  fprintf(stderr, "===== Protocol analysis statistics =====\n");
  fprintf(stderr, "%12u non-IP packets\n", pkts.nonip);
  fprintf(stderr, "%12u IPv6 packets\n", pkts.ipv6);
  fprintf(stderr, "%12u UDP packets\n", pkts.udp);
  fprintf(stderr, "%12u ICMP packets\n", pkts.icmp);
  fprintf(stderr, "%12u other IP packets\n", pkts.other_ip);
  fprintf(stderr, "%12u fragmented packets\n", pkts.frag);
  if (cfg.num_innets > 0) {
    fprintf(stderr, "%12u packets did not match a local network\n", pkts.no_local);
  }
  if (cfg.inbound_only) {
    fprintf(stderr, "%12u SYN packets related to outbound conns\n", pkts.outbound_syn);
  }
  if (cfg.port_whitelist) {
    fprintf(stderr, "%12u packets not on a whitelisted port\n", pkts.not_whitelisted_port);
  }
  if (cfg.ascii) {
    fprintf(stderr, "%12u lines of ASCII input could not be imported\n", pkts.ascii_input_failure);
  }
  fprintf(stderr, "%12u total processed TCP segments (not including above)\n", pkts.processed_tcp);
  fprintf(stderr, "%12u of these were in conns that we missed the start of\n", pkts.missed_start);
  fprintf(stderr, "%12u of these (after conn established) didn't have ACK flag set.\n", pkts.lacked_ack);
}

void
printStats(coral_pkt_stats_t* stats)
{
  fprintf(stderr, "%12llu - layer 2 PDUs received\n", stats->l2_recv);
  fprintf(stderr, "%12llu - layer 2 PDUs dropped\n", stats->l2_drop);
  fprintf(stderr, "%12llu - packets received\n", stats->pkts_recv);
  fprintf(stderr, "%12llu - packets dropped\n", stats->pkts_drop);
  fprintf(stderr, "%12llu - packets truncated by insufficient capture\n", stats->truncated);
  fprintf(stderr, "%12llu - that cell looks bad from the capture device\n", stats->driver_corrupt);
  fprintf(stderr, "%12llu - too many simultaneous vpvc pairs\n", stats->too_many_vpvc);
  fprintf(stderr, "%12llu - AAL5 cell stream > MAX_PACKET_SIZE\n", stats->buffer_overflow);
  fprintf(stderr, "%12llu - AAL5 trailer had bad length\n", stats->aal5_trailer);
  fprintf(stderr, "%12llu - OK\n", stats->ok_packet);
}

void
printIfaceStats()
{
  coral_iface_t *iface = NULL;
  coral_pkt_stats_t *stats = NULL;
  int i = 0;
  while ((iface = coral_next_interface(iface))) {
    stats = (coral_pkt_stats_t*)coral_get_iface_stats(iface);
    fprintf(stderr, "===== Interface #%d =====\n", i++);
    printStats(stats);
  }
  fprintf(stderr, "===== %d interfaces total =====\n", i);
}

void
printIntervalReport(coral_interval_result_t *interval)
{
  if (!cfg.report && !cfg.debug) {
    return;
  }
  fprintf(stderr, "\nStats for interval from %ld.%06ld to %ld.%06ld:\n",
      interval->begin.tv_sec, interval->begin.tv_usec,
      interval->end.tv_sec,   interval->end.tv_usec);
  if (cfg.report) {
    printStats(interval->stats);
  }
  if (cfg.debug) {
    printStorageStats();
  }
}

void
printStorageStats()
{
  storstat open, closed;
  int i, sum1=0, sum2=0, sum3=0, sum4=0;
  storageStats(conns, &open);
  storageStats(closed_conns, &closed);
  fprintf(stderr, "preallocated used/unused: %u/%u (open) %u/%u (closed)\n", open.prealloc_used, open.prealloc_unused, closed.prealloc_used, closed.prealloc_unused);
  fprintf(stderr, "allocated used/unused: %u/%u (open) %u/%u (closed)\n", open.alloc_used, open.alloc_unused, closed.alloc_used, closed.alloc_unused);
  fprintf(stderr, "longest chain: %u (open) %u (closed)\n", open.long_chain, closed.long_chain);
  fprintf(stderr, "frequencies (out of %lu buckets):\n", NUM_BUCKETS);
  fprintf(stderr, "\tnum   OpnLenFreq OpnUsedFrq CloLenFreq CloUsedFrq\n");
  for (i=0; i<9; i++) {
    fprintf(stderr, "\t%3d     %8u   %8u   %8u   %8u\n", i, open.length_freq[i], open.used_freq[i], closed.length_freq[i], closed.used_freq[i]);
    sum1 += open.length_freq[i];
    sum2 += open.used_freq[i];
    sum3 += closed.length_freq[i];
    sum4 += closed.used_freq[i];
  }
  fprintf(stderr, "\t 9+     %8u   %8u   %8u   %8u\n", open.length_freq[9], open.used_freq[9], closed.length_freq[9], closed.used_freq[9]);
  sum1 += open.length_freq[9];
  sum2 += open.used_freq[9];
  sum3 += closed.length_freq[9];
  sum4 += closed.used_freq[9];
  fprintf(stderr, "\ttot     %8d   %8d   %8d   %8d\n", sum1, sum2, sum3, sum4);
  fprintf(stderr, "\tavg     %8.4f   %8.4f   %8.4f   %8.4f\n", open.avg_chain, open.avg_usage, closed.avg_chain, closed.avg_usage);
}

void
parseInputInterfaces(char* optarg)
{
  char *buf = optarg;
  long ifnum;
  int nparsed = 0;

  do {
    ifnum = strtol(buf, NULL, 10);
    if (ifnum < 0) {
      fprintf(stderr, "ERROR: invalid or negative interface number.\n"
          "Do -h for help.\n");
      exit(-1);
    }
    if (cfg.mirrored_iface[(unsigned int)ifnum]) {
      fprintf(stderr, "ERROR: interfaces cannot be both mirrored and inbound.\n"
          "Do -h for help.\n");
      exit(-1);
    }
    cfg.in_iface[(unsigned int)ifnum] = 1;
    nparsed++;
  } while ((buf = strchr(buf, ',') + 1) && buf - optarg < strlen(optarg));

  if (nparsed <= 0) {
    fprintf(stderr, "ERROR: invalid interface argument '%s'.\n"
        "Do -h for help.\n", optarg);
    exit(-1);
  }
  cfg.num_ifaces = nparsed;
}

void
parsePortWhitelist(char* arg)
{
  char *buf = arg;
  long port;
  int nparsed = 0;
  do {
    port = strtol(buf, NULL, 10);
    if (port < 0 || port >= 65536) {
      fprintf(stderr,
          "ERROR: specify at least 1 (valid) port with the -p switch.\n");
      exit(-1);
    }
    cfg.ports[(uint16_t)port] = 1;
    nparsed++;
  } while ((buf = strchr(buf, ',') + 1) && buf - optarg < strlen(optarg));
  if (nparsed <= 0) {
    fprintf(stderr,
        "ERROR: invalid argument to -p: '%s'.\n", optarg);
    exit(-1);
  }
}

void
parseMaxIdle(char *arg)
{
  cfg.max_idle = strtoul(arg, NULL, 10);
  if (errno == EINVAL) {
    fprintf(stderr,
        "ERROR: invalid number passed to -m option.\n"
        "Do -h for help.\n");
    exit(-1);
  } else if (errno == ERANGE) {
    fprintf(stderr,
        "ERROR: number given to -m option was out of range.\n"
        "-m should be specified as seconds.\n");
    exit(-1);
  } else if (cfg.max_idle == 0) {
    fprintf(stderr,
        "WARNING: don't set MAXIDLE to 0.  Chaos will probably ensue.\n");
  }
}

void
parseMirroredIfaces(char* optarg)
{
  char *buf = optarg;
  long ifnum;
  int nparsed = 0;

  do {
    ifnum = strtol(buf, NULL, 10);
    if (ifnum < 0) {
      fprintf(stderr, "ERROR: invalid or negative interface number.\n"
          "Do -h for help.\n");
      exit(-1);
    }
    if (cfg.in_iface[(unsigned int)ifnum]) {
      fprintf(stderr, "ERROR: interfaces cannot be both inbound and mirrored.\n"
          "Do -h for help.\n");
      exit(-1);
    }
    cfg.mirrored_iface[(unsigned int)ifnum] = 1;
    nparsed++;
  } while ((buf = strchr(buf, ',') + 1) && buf - optarg < strlen(optarg));

  if (nparsed <= 0) {
    fprintf(stderr, "ERROR: invalid interface argument '%s'.\n"
        "Do -h for help.\n", optarg);
    exit(-1);
  }
  cfg.num_mirrored = nparsed;
}

void
parseInterval(char *arg)
{
  cfg.interval_secs = strtoul(arg, NULL, 10);
  if (errno == EINVAL) {
    fprintf(stderr,
        "ERROR: invalid number passed to -n option.\n"
        "Do -h for help.\n");
    exit(-1);
  } else if (errno == ERANGE) {
    fprintf(stderr,
        "ERROR: number given to -n option was out of range.\n"
        "-n should be specified as seconds.\n");
    exit(-1);
  }
}

void
parseQuietTime(char *arg)
{
  cfg.quiet_time = strtoul(arg, NULL, 10);
  if (errno == EINVAL) {
    fprintf(stderr,
        "ERROR: invalid number passed to -q option.\n"
        "Do -h for help.\n");
    exit(-1);
  } else if (errno == ERANGE) {
    fprintf(stderr,
        "ERROR: number given to -q option was out of range.\n"
        "-q should be specified as milliseconds.\n");
    exit(-1);
  } else if (cfg.quiet_time < 500) {
    fprintf(stderr,
        "WARNING: don't set -q to < 500ms.  Chaos will probably ensue.\n");
  }
}

void
parseInNet(char *arg)
{
  char *buf = arg;
  char *buf2;
  uint32_t bitmask=0;
  long mask;
  in_addr_t addr;

  if (cfg.num_innets == MAX_INNETS) {
    fprintf(stderr,
        "ERROR: maximum local networks (MAX_INNETS) exceeded\n");
    exit(-1);
  }
  buf2 = strsep(&buf, "/");
  addr = inet_addr(buf2);
  if (addr == INADDR_NONE) {
    fprintf(stderr,
        "ERROR: local network '%s' unparseable: inet_addr() error\n"
        "  (network should be in the form '152.2.0.0/16')\n", optarg);
    exit(-1);
  }

  mask = strtol(buf, NULL, 10);
  if (mask < 1 || mask > 30) {
    fprintf(stderr,
        "ERROR: local network '%s' unparseable: mask must be between 1 and 30.\n", optarg);
    exit(-1);
  }
  for (int i=0; i<mask; i++) {
    bitmask |= 1 << (31-i);
  }
  cfg.innets[cfg.num_innets] = addr;
  cfg.inmask[cfg.num_innets] = bitmask;
  cfg.num_innets += 1;
}

void
parseSocket(char *buff, struct in_addr *addr, unsigned int *port)
{
  unsigned short quad[4];
  if (sscanf(buff, "%hu.%hu.%hu.%hu.%u", quad, quad+1, quad+2, quad+3, port)
      == 5) {
    quad2inet(quad, addr);
  } else if (sscanf(buff, "%hu.%hu.%hu.%hu", quad, quad+1, quad+2, quad+3)
      == 4) {
    *port = 0;
    quad2inet(quad, addr);
  } else {
    error("cannot parse IP/Port combo ('%s') on input line %d\n", buff, segments_read);
  }
}

void
quad2inet(unsigned short quad[4], struct in_addr *address)
{
  char buff2[20];
  sprintf(buff2, "%hu.%hu.%hu.%hu", quad[0], quad[1], quad[2], quad[3]);
  if (inet_aton(buff2, address) == 0) {
    error("Bad IP ('%s') on input line %d\n", buff2, segments_read);
  }
}

void
handleSigInt(int signum)
{
  if (signum != SIGINT) {
    fprintf(stderr, "handleSigInt() got signal number %d\n", signum);
    return;
  }
  fprintf(stderr, "\nSIGINT received; exiting cleanly.\n");
  cleanExit(conns);
}

void
configure(int argc, char** argv)
{
  extern int errno;
  int opt, i;
  extern char *optarg;
  extern int optind;
  struct timeval interval;

  // defaults:
  coral_set_api(CORAL_API_PKT);
  coral_set_options(0, CORAL_OPT_SORT_TIME);
  cfg.binary = 0;
  cfg.inbound_only = 0;
  cfg.track_partials = 0;
  cfg.ascii = 0;
  cfg.ascii_iface = '<';
  cfg.ascii_input = NULL;
  cfg.max_idle = 10;
  cfg.ordinals = 0;
  cfg.quiet_time = 500;
  for (i=0; i<MAX_IFACES; i++) {
    cfg.in_iface[i] = 0;
    cfg.mirrored_iface[i] = 0;
  }
  cfg.num_ifaces = 0;
  cfg.num_mirrored = 0;
  cfg.port_whitelist = 0;
  for (i=0; i<65536; i++) {
    cfg.ports[i] = 0;
  }
  for (i=0; i<MAX_INNETS; i++) {
    cfg.innets[i] = 0;
    cfg.inmask[i] = 0;
  }
  cfg.num_innets = 0;
  cfg.report = 0;
  cfg.rotate_name[0] = '\0';
  cfg.do_rotate = 0;
  cfg.debug = 0;

  while ((opt = getopt(argc, argv, "abC:dIi:l:m:M:n:op:Pq:r:s?h")) != -1) {
    switch (opt) {
      case 'a':
        cfg.ascii = 1;
        break;

      case 'b':
        cfg.binary = 1;
        break;

      case 'C':
        if (coral_config_command(optarg) < 0)
          exit(-1);
        break;

      case 'd':
        cfg.debug = 1;
        break;

      case 'I':
        cfg.inbound_only = 1;
        break;

      case 'i':
        parseInputInterfaces(optarg);
        break;

      case 'l':
        parseInNet(optarg);
        break;

      case 'm':
        parseMaxIdle(optarg);
        break;

      case 'M':
        parseMirroredIfaces(optarg);
        break;

      case 'n':
        parseInterval(optarg);
        break;

      case 'o':
        cfg.ordinals = 1;
        break;

      case 'p':
        cfg.port_whitelist = 1;
        parsePortWhitelist(optarg);
        break;

      case 'P':
        cfg.track_partials = 1;
        break;

      case 'q':
        parseQuietTime(optarg);
        break;

      case 'r':
        cfg.do_rotate = 1;
        strncpy(cfg.rotate_name, optarg, 1022);
        break;

      case 's':
        cfg.report = 1;
        break;

      case '?':
        fprintf(stderr,
            "  adudump -a [-i '>'] [OPTIONS] source.tcpasc\n"
            "  adudump -i A[,B,..] [-M C[,D,..]] [OPTIONS] INPUT ...\n"
            "  adudump -M C[,D,..] [OPTIONS] INPUT ...\n"
            "  adudump -l NET1 -l NET2 [OPTIONS] INPUT ...\n\n"
            "OPTIONS: -b -CCRL_CMD -d -I -mMAXIDLE -nSECS -o -pA[,B] -P -qQUIET -rNAME -s\n"
            "\ndo -h for full help screen\n"
            );
        exit(0);
        break;

      case 'h':
      default:
        coral_usage(argv[0],
            "Print ADUs and think-times for TCP traffic.\n\n"
            "usage:\n"
            "  adudump -a [-i '>'] [OPTIONS] source.tcpasc\n"
            "  adudump -i A[,B,..] [-M C[,D,..]] [OPTIONS] INPUT ...\n"
            "  adudump -M C[,D,..] [OPTIONS] INPUT ...\n"
            "  adudump -l NET1 -l NET2 [OPTIONS] INPUT ...\n\n"
            "-a        ASCII input (from tcpdump output)\n"
            "-i A[,B]  inbound interface(s) (comma-separated)\n"
            "          for ascii input, defaults to '<' records\n"
            "-M C[,D]  port-mirrored interface(s) (comma-separated)\n"
            "-l NET    Local network, e.g. '152.2.0.0/16'\n\n"
            "INPUT: offline (e.g. trace.dag or trace.pcap) or live (e.g. /dev/dag0)\n\n"
            "OPTIONS:\n"
            "-b         binary output\n"
            "-C COMMAND <coral_command> (see below)\n"
            "-d         debug data structures (uses -n)\n"
            "-I         only observe inbound connections (doesn't apply to -M ifaces)\n"
            "-m MAXIDLE allow auto-purging of connections idle for at least MAXIDLE seconds\n"
            "           MAXIDLE defaults to 10 seconds.\n"
            "-n SECS    iNterval for reports (if -s) and rotations (if -r).\n"
            "-o         print ordinals (i.e. i-th ADU in connection)\n"
            "-p A[,B]   only observe connections to one of these ports\n"
            "-P         track partial connections [EXPERIMENTAL]\n"
            "-q QUIET   inter-ADU quiet-time threshold (in ms)\n"
            "           quiet-times this large and larger split ADUs\n"
            "           defaults to 500ms\n"
            "-r NAME    rotate output, with filenames expanded from NAME.\n"
            "           NAME can include %%i (iteration counter), %%s (seconds since start),\n"
            "           %%f (6 digits of fractional seconds), %%F (YYYY-MM-DD),\n"
            "           or any strftime() expansions.\n"
            "-s         print packet stats report on stderr at end\n"
            );
        exit(-1);
    }
  }

  // Error checking:
  if (!cfg.ascii) {
    if ((cfg.num_ifaces > 0 || cfg.num_mirrored > 0) && cfg.num_innets > 0) {
      fprintf(stderr, "ERROR: specify either local networks or inbound/mirrored interfaces, but not both.\n");
      exit(-1);
    }
    if (cfg.num_ifaces == 0 && cfg.num_mirrored == 0 && cfg.num_innets == 0) {
      fprintf(stderr, "ERROR: specify at least 1 inbound or mirrored interface\n"
          "or at least 1 local network.\n"
          "Do -h for help.\n");
      exit(-1);
    }
    if (optind >= argc) {
      fprintf(stderr, "ERROR: you must specify at least 1 input file/device.\n"
          "Do -h for help.\n");
      exit(-1);
    }

    while (optind < argc) {
      if (!coral_new_source(argv[optind])) {
        exit(-1);
      }
      optind++;
    }

  } else {
    if (optind >= argc) {
      fprintf(stderr, "ERROR: when using -a, you must specify an input file.\n"
          "Do -h for help.\n");
      exit(-1);
    }
    if (strcmp(argv[optind], "-") == 0) {
      cfg.ascii_input = stdin;
    } else {
      cfg.ascii_input = fopen(argv[optind], "r");
      if (cfg.ascii_input == NULL) {
        perror("opening ascii file for reading");
        exit(-1);
      }
    }
  }

  if (cfg.ordinals && cfg.binary) {
    fprintf(stderr, "ERROR: sorry, but binary output of ordinals is not supported yet.\n");
    exit(-1);
  }

  if (cfg.interval_secs) {
    interval.tv_sec = cfg.interval_secs;
    interval.tv_usec = 0;
    if (coral_set_interval(&interval) != 0) {
      error("error with coral_set_interval()");
    }
  }

  if (cfg.do_rotate) {
    coral_rotfile_t *tmp;
    tmp = coral_rf_open_file(&cfg.out, cfg.rotate_name, NULL, CORAL_ROT_AUTO);
    if (tmp == NULL) {
      perror("error with coral_rf_open_file()");
      exit(-1);
    }
  } else {
    cfg.out = stdout;
  }
}

int
main(int argc, char** argv)
{
  coral_iface_t *iface;
  coral_pkt_result_t pkt;
  coral_interval_result_t ival;
  struct sigaction sa;

  //I tried statically allocating this, and it clobbered the stack.
  conns = (connection**) malloc(NUM_BUCKETS * sizeof(connection*));
  closed_conns = (connection**) malloc(NUM_BUCKETS * sizeof(connection*));

  init(conns, closed_conns);
  configure(argc, argv);
  segments_read = 0;
  if (!cfg.ascii) {
    setupSources();
  }

  sa.sa_handler = handleSigInt;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (sigaction(SIGINT, &sa, NULL) != 0) {
    perror("error setting handler for SIGINT with sigaction()");
  }

  if (cfg.ascii) {
    while (asciiPacketHandler()) {
      segments_read++;
    }
  } else {
    while (1) {
      iface = coral_read_pkt(&pkt, &ival);
      if (!iface) {
        if (errno != 0) {
          perror("error reading a packet with coral_read_pkt()");
        }
        break;
      }
      if (pkt.packet) {
        packetHandler(iface, &pkt);
      } else if (ival.stats == NULL) {
        // a new interval just started at time ival.begin.
      } else {
        // an interval just ended.
        printIntervalReport(&ival);
      }
      segments_read++;
    }
  }

  if (cfg.debug) {
    printStorageStats();
  }
  cleanExit(conns);
}


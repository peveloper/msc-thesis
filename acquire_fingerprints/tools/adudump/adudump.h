#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <pcap.h>
#include "lookup3.c"
#include <sys/param.h>
#include <libcoral.h>
#include <coral-config.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>

// XXX - I should figure out how to do this properly...
#define __FAVOR_BSD
#include <netinet/tcp.h>

#define SEQNO_ABS_DIFF(a,b) (((a) < (b) ? (b) - (a) : (a) - (b)))


/********************************************************
 *                       Layer 1                        *
 ********************************************************/
void setupSources();
void closeSources();
int isIncomingIface(coral_iface_t*);
int isMirroredIface(coral_iface_t*);
int isLocalNet(struct in_addr*);
void packetHandler(coral_iface_t*, coral_pkt_result_t*);
int asciiPacketHandler();

/* IP header */
struct sniff_ip
{
  #if BYTE_ORDER == LITTLE_ENDIAN
  u_int ip_hl:4,                           // header length
  ip_v:4;                                  // version
  #if BYTE_ORDER == BIG_ENDIAN
  u_int ip_v:4,                            // version
  ip_hl:4;                                 // header length
  #endif
  #endif                                   // not _IP_VHL
  u_char  ip_tos;                          // type of service
  u_short ip_len;                          // total length
  u_short ip_id;                           // identification
  u_short ip_off;                          // fragment offset field
  #define IP_RF           0x8000           // reserved fragment flag
  #define IP_DF           0x4000           // dont fragment flag
  #define IP_MF           0x2000           // more fragments flag
  #ifndef IP_OFFMASK
  #define IP_OFFMASK      0x1fff           // mask for fragmenting bits
  #endif
  u_char  ip_ttl;                          // time to live
  u_char  ip_p;                            // protocol
  u_short ip_sum;                          // checksum
  struct  in_addr         ip_src, ip_dst;  // source and dest address
};

struct sniff_tcp
{
  u_int16_t source;
  u_int16_t dest;
  u_int32_t seq;
  u_int32_t ack_seq;
#  if __BYTE_ORDER == __LITTLE_ENDIAN
  u_int16_t res1:4;
  u_int16_t doff:4;
  u_int16_t fin:1;
  u_int16_t syn:1;
  u_int16_t rst:1;
  u_int16_t psh:1;
  u_int16_t ack:1;
  u_int16_t urg:1;
  u_int16_t res2:2;
#  elif __BYTE_ORDER == __BIG_ENDIAN
  u_int16_t doff:4;
  u_int16_t res1:4;
  u_int16_t res2:2;
  u_int16_t urg:1;
  u_int16_t ack:1;
  u_int16_t psh:1;
  u_int16_t rst:1;
  u_int16_t syn:1;
  u_int16_t fin:1;
#  else
#   error "Adjust your <bits/endian.h> defines"
#  endif
  u_int16_t window;
  u_int16_t check;
  u_int16_t urg_ptr;
};


/********************************************************
 *                       Layer 2                        *
 ********************************************************/
unsigned int hashConnection(unsigned long, unsigned long, unsigned int, unsigned int);
struct timeval elapsedTime(struct timeval*, struct timeval*);
double elapsedMS(struct timeval*, struct timeval*);

#define NUM_BUCKETS 250000UL
#define BUCKET_SIZE 3
enum conn_states {UNUSED, NEW_SYN, NEW_SYNACK, HALF_OPEN, MOSTLY_OPEN, SEQUENTIAL, CONCURRENT, PARTIAL};
enum flow_states {INACTIVE, ACTIVE, CLOSED};

/* The Segment structure is used to hold the result of parsing the tcpdump
 * ascii format of a trace entry to obtain the fields used by the selection
 * algorithm. 
 */
typedef struct {
  struct timeval ts;
  struct in_addr loc_addr;  /* local (internal) IP */
  struct in_addr rem_addr;  /* remote (external) IP */
  uint8_t dir;  /* direction this segment addressed; */
              /* 0 == > (local IP to remote), 1 == < (remote IP to local) */
  uint32_t start_seqno; /* seqno of the first byte in the payload */
                             /* note that SYN and FIN consume one seqno */
  uint32_t next_seqno;  /* next seqno available = start + length */
  uint32_t ackno;       /* ackno is the first unack'd seqno (next_seqno) */
  uint8_t flags;  /* SYN, RST, FIN, ACK, etc. */
  uint16_t loc_port;
  uint16_t rem_port;
  uint16_t payload;
  uint16_t iface;
} segment;

/* The Flow structure is used to track the state of ADUs in one direction of a
 * TCP connection.  This is the fundamental data structure used by the
 * selection algorithm.
 */
typedef struct flow_state {
  enum flow_states state;  /* state w.r.t. sending an ADU */
  uint32_t init_seqno;
  uint32_t next_seqno; /* next sequence number expected */
  uint32_t init_ackno;
  uint32_t high_ackno; /* highest ACK number seen */
  struct timeval last_send; /* last time a new segment sent in this direction */
  uint32_t adu_seqno;  /* seqno of start of last ADU */
  uint8_t iface;
} flow;  

/* The Connection structure is used to hold information associated with a TCP
 * connection.  It is the fundamental entry in the hash table that is used to
 * quickly locate the connection state given the canonical 4-tuple of a
 * segment.
 */
typedef struct connection_state {
  uint8_t use_state;   /* 0 == no valid connection state, 1 == valid state */
  void *next_in_bucket;  /* link connection entries in hash bucket */
  struct in_addr IPAddr1; /* canonical 4-tuple here */
  struct in_addr IPAddr2;
  uint16_t Port1;
  uint16_t Port2;
  enum conn_states state; /* state w.r.t SYN exchange & type (SEQ vs CONC) */
  int8_t init_dir; /* direction indicator for connection initiator */
  int8_t acpt_dir; /* direction indicator for connection acceptor */
  int16_t ord_major; /* number of think-times seen so far */
  int16_t ord_minor; /* number of ADU within sequence of same-dir ADUs */
  flow flows[2]; /* there is a "flow" for each direction of a TCP connection */
} connection;

typedef struct hash_key_tuple {
  struct in_addr IPAddr1;
  struct in_addr IPAddr2;
  uint16_t Port1;
  uint16_t Port2;
} hash_key;

struct {
  // other network protocols:
  uint32_t nonip;
  uint32_t ipv6;
  // other transport protocols:
  uint32_t udp;
  uint32_t icmp;
  uint32_t other_ip;
  // reasons to discard a packet at input routines
  uint32_t frag;
  uint32_t no_local; // with -l switch, neither address was "local".
  uint32_t outbound_syn;
  uint32_t not_whitelisted_port;
  uint32_t ascii_input_failure;
  // all processed TCP segments
  uint32_t processed_tcp;
  // reasons to discard while analyzing TCP segment
  uint32_t missed_start;
  uint32_t lacked_ack;
} pkts;


/********************************************************
 *                       Layer 3                        *
 ********************************************************/
typedef struct {
  uint32_t prealloc_used;
  uint32_t prealloc_unused;
  uint32_t alloc_used;
  uint32_t alloc_unused;
  float avg_chain;
  float avg_usage;
  uint16_t long_chain;
  uint32_t length_freq[10];
  uint32_t used_freq[10];
} storstat;

connection *lookupConn(segment*, connection**);
connection *createConn(segment*, connection**);
void init(connection**, connection**);
void cleanup(connection**);
void storageStats(connection**, storstat*);
void freeBucket(connection*);
unsigned long long aduSize(flow*);
void reuseConn(segment*, connection*);
void endConn(segment*, connection*);
void resetConn(connection*);
int maxIdleTimeout(struct timeval*, struct timeval*);
int timevalGreaterThan(struct timeval*, struct timeval*);
void updateTime(flow*, struct timeval*);
void cleanExit(connection**);
connection **conns;
connection **closed_conns;


/********************************************************
 *                       Layer 4                        *
 ********************************************************/
// XXX - should accept connection **conns as well
void processSegment(segment*);
int startTrackingConnNow(segment*);
void handleNewConn(segment*, connection*);
void handleNewSynAckConn(segment*, connection*);
void handleHalfOpenConn(segment*, connection*);
void handleMostlyOpenConn(segment*, connection*);
void handlePartialConn(segment*, connection*);
void handleOpenConn(segment*, connection*);
void handleSeqInactiveFlow(segment*, connection*);
void handleSeqActiveFlow(segment*, connection*);
void handleOpenConnFin(segment*, connection*);
void handleOpenConnRst(segment*, connection*);
unsigned long guessADUStart(connection*, segment*);
void checkQuietTime(segment*, connection*);
int aduIdleTimeout(struct timeval*, struct timeval*);
void translateConcOrdinals(connection*);


/********************************************************
 *                       Layer 5                        *
 ********************************************************/
void error(char *, ...);
void printTextHelper(struct timeval*, connection*, char*, char, int, char*);
void printBinaryHelper(struct timeval*, connection*, char, uint8_t);
void printSynRecord(struct timeval*, connection*, uint8_t);
void printSynAckRecord(struct timeval *, connection*, uint8_t);
void printRttRecord(struct timeval*, connection*, uint8_t);
void printSeqRecord(struct timeval*, connection*, uint8_t);
void printConcRecord(struct timeval*, connection*);
void printPartRecord(struct timeval*, connection*, uint8_t);
void printEndRecord(struct timeval*, connection*);
void printIncRecord(connection*, int);
void printAduRecord(struct timeval*, connection*, int, struct timeval*, uint8_t);
void printPacketStats();
void printStats(coral_pkt_stats_t*);
void printIfaceStats();
void printIntervalReport(coral_interval_result_t*);
void printStorageStats();
void parseInputInterfaces(char*);
void parsePortWhitelist(char*);
void parseMaxIdle(char*);
void parseMirroredIfaces(char*);
void parseInterval(char*);
void parseQuietTime(char*);
void parseInNet(char*);
void parseSocket(char*, struct in_addr*, unsigned int*);
void quad2inet(unsigned short[4], struct in_addr*);
void handleSigInt(int);
void configure(int, char**);
int main(int, char**);

#define MAX_IFACES 20
#define MAX_INNETS 20
struct {
  uint8_t binary;
  uint8_t inbound_only;
  uint8_t track_partials;
  uint8_t in_iface[MAX_IFACES];
  uint8_t mirrored_iface[MAX_IFACES];
  uint8_t num_ifaces;
  uint8_t num_mirrored;
  uint8_t port_whitelist;
  uint8_t ports[65536];
  uint8_t ascii;
  char ascii_iface;
  FILE* ascii_input;
  uint32_t max_idle;
  uint32_t interval_secs;
  uint8_t ordinals;
  uint32_t quiet_time;
  uint32_t innets[MAX_INNETS];
  uint32_t inmask[MAX_INNETS];
  uint8_t num_innets;
  uint8_t report;
  char rotate_name[1024];
  uint8_t do_rotate;
  FILE* out;
  uint8_t debug;
} cfg;


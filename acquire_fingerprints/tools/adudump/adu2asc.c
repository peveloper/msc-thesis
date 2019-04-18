#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

#define SCAN4(a, b, x) a[0]=b[x]; a[1]=b[x+1]; a[2]=b[x+2]; a[3]=b[x+3];
#define SCAN2(a, b, x) a[0]=b[x]; a[1]=b[x+1];
#define PRINT_IP_PORT(ip, pt) ip[0], ip[1], ip[2], ip[3], (pt[0]*256)+pt[1]
#define PRINT_TS(ts1, ts2) (ts1[0] << 24 | ts1[1] << 16 | ts1[2] << 8 | ts1[3]), (ts2[0] << 24 | ts2[1] << 16 | ts2[2] << 8 | ts2[3])

int rec_len[] = {0, 22, 30, 22, 22, 39, 22, 31};

void printRec(char type, unsigned char* buf);

int main(int argc, char** argv)
{
  int fd;
  int n, i;
  unsigned char type[1];
  unsigned char buf[40];

  if (argc != 2) {
    fprintf(stderr, "Specify a file to read (- for stdin).\n");
    return 1;
  }

  if (strncmp(argv[1], "-", 1) == 0) {
    fprintf(stderr, "Reading stdin is known to cause problems.\n"
        "Until I debug it, this functionality is disabled.\n");
    exit(-1);
    if ((fd = open("/dev/stdin", O_RDONLY)) == -1) {
      perror("open (of stdin) failed");
      return 1;
    }
  } else {
    if ((fd = open(argv[1], O_RDONLY)) == -1) {
      perror("open (of a file) failed");
      return 1;
    }
  }

  while ((n = read(fd, type, 1)) != 0) {
    if (n < 1) {
      perror("error on read of 1 byte");
      return 1;
    }
    if (n > 1) {
      fprintf(stderr, "huh...read %d bytes with read(fd, type, 1) call\n", n);
    }

    n = 0;
    do {
      i = read(fd, buf, rec_len[type[0]] - n);
      if (i < 0) {
        perror("error reading multiple bytes");
        return 1;
      } else if (i == 0) {
        // read might not have any more data to read...sleep a second and try
        // again.
        sleep(1);
        i = read(fd, buf, rec_len[type[0]] - n);
        if (i == 0) {
          return 1;
        }
      }
      n += i;
    } while (n < rec_len[type[0]]);
    printRec(type[0], buf);
  }

  close(fd);
  return 0;
}

void printRec(char type, unsigned char* buf)
{
  unsigned char ip1[4], ip2[4], port1[2], port2[2], dir, el1[4], el2[4], state;
  unsigned char ts1[4], ts2[4];
  unsigned long long sz1[4], sz2[4];

  switch (type) {
    case 1:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      printf("SYN: %u.%06u %u.%u.%u.%u.%u %c %u.%u.%u.%u.%u\n",
          PRINT_TS(ts1, ts2),
          PRINT_IP_PORT(ip1, port1),
          (dir == 1 ? '>' : '<'),
          PRINT_IP_PORT(ip2, port2));
      break;

    case 2:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      SCAN4(el1, buf, 21); SCAN4(el2, buf, 25);
      printf("RTT: %u.%06u %u.%u.%u.%u.%u %c %u.%u.%u.%u.%u %u.%06u\n",
          PRINT_TS(ts1, ts2),
          PRINT_IP_PORT(ip1, port1),
          (dir == 1 ? '>' : '<'),
          PRINT_IP_PORT(ip2, port2),
          PRINT_TS(el1, el2));
      break;

    case 3:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      printf("SEQ: %u.%06u %u.%u.%u.%u.%u %c %u.%u.%u.%u.%u\n",
          PRINT_TS(ts1, ts2),
          PRINT_IP_PORT(ip1, port1),
          (dir == 1 ? '>' : '<'),
          PRINT_IP_PORT(ip2, port2));
      break;

    case 4:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      printf("CONC: %u.%06u %u.%u.%u.%u.%u x %u.%u.%u.%u.%u\n",
          PRINT_TS(ts1, ts2),
          PRINT_IP_PORT(ip1, port1),
          PRINT_IP_PORT(ip2, port2));
      break;

    case 5:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      SCAN4(sz1, buf, 21); SCAN4(sz2, buf, 25);
      state = buf[29];
      SCAN4(el1, buf, 30); SCAN4(el2, buf, 34);
      if (ts1[0] == 0 && ts1[1] == 0 && ts1[2] == 0 && ts1[3] == 0) {
        printf("ADU: X.X ");
      } else {
        printf("ADU: %u.%06u ", PRINT_TS(ts1, ts2));
      }
      printf("%u.%u.%u.%u.%u %c %u.%u.%u.%u.%u %llu %s",
          PRINT_IP_PORT(ip1, port1),
          (dir == 1 ? '>' : '<'),
          PRINT_IP_PORT(ip2, port2),
          (sz1[0] << 56 | sz1[1] << 48 | sz1[2] << 40 | sz1[3] << 32 |
           sz2[0] << 24 | sz2[1] << 16 | sz2[2] << 8 | sz2[3]),
          (state == 3 ? "SEQ" : "CONC"));
      if (el1[0] != 0 || el1[1] != 0 || el1[2] != 0 || el1[3] != 0 ||
          el2[0] != 0 || el2[1] != 0 || el2[2] != 0 || el2[3] != 0) {
        printf(" %u.%06u", PRINT_TS(el1, el2));
      }
      printf("\n");
      break;

    case 6:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      printf("END: %u.%06u %u.%u.%u.%u.%u x %u.%u.%u.%u.%u\n",
          PRINT_TS(ts1, ts2),
          PRINT_IP_PORT(ip1, port1),
          PRINT_IP_PORT(ip2, port2));
      break;

    case 7:
      SCAN4(ts1, buf, 0); SCAN4(ts2, buf, 4);
      SCAN4(ip1, buf, 8); SCAN2(port1, buf, 12);
      dir = buf[14];
      SCAN4(ip2, buf, 15); SCAN2(port2, buf, 19);
      SCAN4(sz1, buf, 21); SCAN4(sz2, buf, 25);
      state = buf[29];
      printf("INC: X.X %u.%u.%u.%u.%u %c %u.%u.%u.%u.%u %llu %s\n",
          PRINT_IP_PORT(ip1, port1),
          (dir == 1 ? '>' : '<'),
          PRINT_IP_PORT(ip2, port2),
          (sz1[0] << 56 | sz1[1] << 48 | sz1[2] << 40 | sz1[3] << 32 |
           sz2[0] << 24 | sz2[1] << 16 | sz2[2] << 8 | sz2[3]),
          (state == 3 ? "SEQ" : "CONC"));
      break;

    default:
      fprintf(stderr, "bad record type\n");
  }
}


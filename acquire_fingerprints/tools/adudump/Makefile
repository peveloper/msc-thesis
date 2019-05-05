CORAL_DIR = /usr/local/Coral
CORAL_LIBS = -lcoral -lz -lpcap -lstdc++
PCAP_PREFIX = /usr/local
DIST_FILES = Makefile adudump.c lookup3.c adu2asc.c crunch-adus.sh filter-conns.pl locals.pl makecdf.pl per-server.pl

all: adudump adu2asc

adudump: adudump.c adudump.h
	gcc -g -Wall -std=gnu99 -o adudump adudump.c -I$(PCAP_PREFIX)/include -I$(CORAL_DIR)/include -L$(PCAP_PREFIX)/lib -L$(CORAL_DIR)/lib $(CORAL_LIBS)

adu2asc: adu2asc.c
	gcc -g -Wall -std=gnu99 -o adu2asc adu2asc.c

clean:
	rm -f *.o *.core adu2asc adudump adudump.tar.gz

peledist: adudump.tar.gz
	scp adudump.tar.gz pele:adudump/
	ssh pele "cd ~/adudump; tar -xzvf adudump.tar.gz; make"

adudump.tar.gz: $(DIST_FILES)
	tar -czvf adudump.tar.gz $(DIST_FILES)


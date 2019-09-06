import sys
import time
from socket import *
from collections import deque
from collections import OrderedDict

class Tracker:

    def __init__(self, ip_pair, capture_filename, window_size, key_mode):

        self.addresses = ip_pair
        self.window = deque([], window_size)

        self.id = -1
        self.bitrate = -1
        self.index = -1
        self.capture_index = -1
        self.key = None
        self.capture_key = None
        self.correlation = -1
        self.elapsed = -1
        self.matches = []
        self.filename = None
    
    def add_ADU(self, size, client_socket, idx):
        self.window.append(size)

        if (len(self.window) == window_size):
            output = str(self.window[0])
            for x in range(1, window_size):
                output += "," + str(self.window[x])
            client_socket.send(output + "\n")

            result = client_socket.recv(1024)
            if result != "none\n":

                tokens = result.split("\t")[:-1]
                
                self.id = tokens[0]
                self.bitrate = float(tokens[1])
                self.index = int(tokens[2])
                self.capture_index = idx + 1 - window_size
                self.key = tokens[3].split()
                self.capture_key = tokens[4].split()
                self.correlation = float(tokens[5])

                if (self.filename == None):
                    # Set filename to store matches
                    matches_dir = "../matches"
                    self.filename = "%s/%s_%d_%d_%d" % (matches_dir, capture_filename, window_size, len(self.key), key_mode)

                self.dump_match()
                return 1
            return 0

    def get_last_time(self):
        return self.last_time

    def dump_match(self):
        record = ""
        with(open(self.filename, 'a')) as f:
            record += "%s" % self.id
            record += "\t%.1f" % self.bitrate
            record += "\t%d" % self.index
            record += "\t%d" % self.capture_index
            record += "\t%s" % ' '.join(self.key)
            record += "\t%s" % ' '.join(self.capture_key)
            record += "\t%.16f\n" % self.correlation
            f.write(record)


### Main Program ###
start_time = time.time()

server_name = sys.argv[1]
server_port = int(sys.argv[2])
capture_filename = sys.argv[3]
window_size = int(sys.argv[4])
key_mode = int(sys.argv[5])

client_socket = socket(AF_INET, SOCK_STREAM)
client_socket.connect((server_name,server_port))

cnx_tracker = OrderedDict()

for i, adu in enumerate(sys.stdin):
    adu = adu.rsplit('\n') # remove trailing newline
    fields = adu[0].split()

    try:
        cnx_key = fields[1].split('>')[0].split('.')[0]
        size = int(fields[2])
    except ValueError:
        continue

    if cnx_key in cnx_tracker:
        cnx = cnx_tracker[cnx_key]
        del cnx_tracker[cnx_key]
    else:
        cnx = Tracker(cnx_key, capture_filename, window_size, key_mode)

    cnx.add_ADU(size, client_socket, i)
    cnx_tracker[cnx_key] = cnx

client_socket.send("complete\n")
client_socket.close()

print(time.time() - start_time)

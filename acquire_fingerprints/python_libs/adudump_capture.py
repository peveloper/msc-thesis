import psutil
import subprocess
import time
import os
import signal
import select

from queue import Queue, Empty
from threading import Thread
from .nsbstreamreader import NonBlockingStreamReader as NBSR
from .config import StaticConfig

static_config = StaticConfig()


class AdudumpCapture:
    __process = None
    __log_file = None
    __local_ip = None
    __interface = None
    __t = None
    __q = None

    def __init__(self, local_ip, interface, file):
        self.__local_ip = local_ip
        self.__interface = interface
        self.__log_file = file

    
    def __enter__(self):
        self.__start_adudump()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.__kill_adudump()
        return self

    def get_process(self):
        return self.__process

    def get_log_file(self):
        return self.__log_file

    def output_reader(self):
        print('called')
        for line in iter(self.__process.stdout.readline, b''):
            if line:
                self.__q.put(line)

    def __start_adudump(self):
        """
        starts the adudump executable
        """
        os.system("sudo tcpdump -i " + self.__interface + " net 45 -s 0 -w " + static_config.log_dir + "/" + self.__log_file + " &")

        # wait ten seconds for it to settle
        i = 10
        while i > 0:
            print("waiting " + str(i))
            time.sleep(1)
            i -= 1

        return

    @staticmethod
    def __kill_adudump():
        """
        kills the adudump executable
        """
        os.system('sudo pkill -TERM -f \'.*tcpdump.*\'')
        print("found & killed tcpdump")
        return

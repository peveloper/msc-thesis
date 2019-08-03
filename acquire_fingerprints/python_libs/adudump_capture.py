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
    # __filename = None

    def __init__(self, local_ip, interface, file):
        self.__local_ip = local_ip
        self.__interface = interface
        self.__log_file = file
        # self.__q = Queue()

    
    def __enter__(self):
        # self.__start_adudump()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):

        # self.__process.terminate()
        # self.__process.wait(0.2)
        # self.__t.join()

        # while True:
          # if y.poll(1):
             # print(self.__process.stderr.readline())

        # os.killpg(self.__process.pid, signal.SIGINT)
        # nbsr = NBSR(self.__process.stdout)
        # print('exiting log..')

        # while True:
            # output = self.__nbsr.readline()
            # if not output:
                # break
            # print(output)
        # self.__log_file.flush()
        # while True:
            # try:
                # print(self.__q.get_nowait())
            # except Empty:
                # print('empty')
        # self.__t.join()
        self.__kill_adudump()

        
        # self.__process.kill()
        # self.__log_file.close()
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
                
            # self.__log_file.write(line)
            # print('got line: {0}'.format(line.decode('utf-8')), end='')

    def start_adudump(self):
        """
        starts the adudump executable
        """
        # self.__log_file = open(
            # static_config.log_dir + "/" + self.__filename, "w"
        # )

        # start executable, and save logfile to
        # self.__process = subprocess.Popen(
            # "sudo " + static_config.adudump_dir + "/adudump -q 500 -C ipfilter='net 45' -C norm=0 -l " + 
            # self.__local_ip + " if:" + self.__interface,
            # shell=True, universal_newlines=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        # )

        os.system("sudo " + static_config.adudump_dir + "/adudump -q 500 -C ipfilter='net 45' -C norm=0 -l " + 
            self.__local_ip + " if:" + self.__interface + " > " + self.__log_file + " 2>&1 &")

        # self.__t = Thread(target=self.output_reader)
        # self.__t.daemon = True
        # self.__t.start()

        # self.__nbsr = NBSR(self.__process.stdout)

        # def reader(f,buffer):
           # while True:
             # line=f.readline()
             # if line:
                # buffer.append(line)
             # else:
                # break

        # t=Thread(target=reader,args=(self.__process.stdout, self.__linebuffer))
        # t.daemon=True
        # t.start()


        # while self.__process.poll() is None:
            # l = self.__process.stdout.readline() # This blocks until it receives a newline.
            # print(l)
        # When the subprocess terminates there might be unconsumed output 
        # that still needs to be processed.

        # wait three seconds for it to settle
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

        
        time.sleep(120)
        os.system('sudo pkill -INT -f \'.*adudump.*\'')

        # kill adudump
        print("found & killed adudump")
        return

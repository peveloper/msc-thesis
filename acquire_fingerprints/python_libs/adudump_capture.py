import psutil
import subprocess
import time

from .nsbstreamreader import NonBlockingStreamReader as NBSR
from .config import StaticConfig

static_config = StaticConfig()


class AdudumpCapture:
    __process = None
    __log_file = None
    __local_ip = None
    __interface = None

    def __init__(self, local_ip, interface, filename):
        print(filename)
        self.__local_ip = local_ip
        self.__interface = interface
        self.__log_file = open(
            static_config.log_dir + "/" + filename, "w"
        )

    def __enter__(self):
        self.__start_adudump()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        nbsr = NBSR(self.__process.stdout)
        print('exiting log..')

        while True:
            output = nbsr.readline(0.1)
            if not output:
                break
            self.__log_file.write(output)
            self.__log_file.flush()

        # self.__process.kill()
        # self.__kill_adudump()
        self.__log_file.close()
        return self

    def __start_adudump(self):
        """
        starts the adudump executable
        """

        # start executable, and save logfile to
        self.__process = subprocess.Popen(
            "sudo " + static_config.adudump_dir + "/adudump -l " + self.__local_ip +
            " if:" + self.__interface,
            shell=True, universal_newlines=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # wait three seconds for it to settle
        i = 3
        while i > 0:
            print("waiting " + str(i))
            time.sleep(1)
            i -= 1

    @staticmethod
    def __kill_adudump():
        """
        kills the adudump executable
        """

        # get all current active processes by name
        dictionary = {}
        for process in psutil.process_iter():
            dictionary[process.pid] = process

        # kill adudump
        for pid in sorted(dictionary):
            if 'adudump' in dictionary[pid].name():
                dictionary[pid].kill()
                print("found & killed adudump")
                return

import pickle
import sys
import os
import json
import time
import requests as request
import datetime
import psutil
import subprocess
import time
import json


class BandwidthManipulator:

    def __init__(self, interface, direction):
        self.__interface = interface
        self.__direction = direction

    def __enter__(self):
        self.ensure_sudo()
        self.clear_rate()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.clear_rate()

    @staticmethod
    def ensure_sudo():
        os.system("sudo ls > /dev/null")

    def set_rate(self, rate: str="10Mbit/s"):
        subprocess.Popen(
            "tcset --device " + self.__interface +
            " --direction " + self.__direction +
            " --rate " + rate +
            " --overwrite",
            shell=True, universal_newlines=True, stdout=sys.stdout, stderr=sys.stderr
        )

    def clear_rate(self):
        subprocess.Popen(
            "tcdel --device " + self.__interface +
            " --all",
            shell=True, universal_newlines=True, stdout=sys.stdout, stderr=sys.stderr
        )


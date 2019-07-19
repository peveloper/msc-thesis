#!/usr/bin/python

import datetime
import time
import humanfriendly
import subprocess

from python_libs.config import StaticConfig
from python_libs.config import Inventory
from python_libs.netflix_browser import NetflixBrowser
from python_libs.adudump_capture import AdudumpCapture
from python_libs.bandwidth_manipulator import BandwidthManipulator


config = StaticConfig()
inventory = Inventory()

video_db = inventory.get_movies()
genres = inventory.get_genres() 
# not in swiss catalogue: 19
video_ids = inventory.get_selection_of_movies_by_genre()[44:]

print("Capturing %d titles at %d bandwidth levels" % (len(video_ids), len(config.throughputs)))

# calculate length of capture for user
loop_count = len(config.throughputs)
video_count = len(video_ids)
loop_length = (config.capture_duration + config.wait_after_throughput_adjustment)
full_length = video_count * (loop_count * loop_length)
print("This capture will run for " + humanfriendly.format_timespan(full_length))

local_ip = config.local_ip
interface = config.network_device

def capture_movie(browser, local_ip, interface, filename, video_id, throughput):
    finished = False
    # initialize the adudump capture
    with AdudumpCapture(local_ip, interface, filename) as capture:

        print(
            "capturing at " + str(throughput) + "kbps for " +
            str(config.capture_duration) + "s"
        )


        if browser.navigate(video_id, throughput):
            return True
            # finished = True
            # print('here')
            # capture.__exit__()

    # if finished:
        # time.sleep(10)
        # return True

    return False

def main():
    # initialize the bandwidth
    with BandwidthManipulator(interface, "incoming") as bandwidth:

        # initialize the browser
        with NetflixBrowser() as browser:

            for video in video_ids:
                video_id = video

                for throughput in config.throughputs:

                    # inform user
                    print("Capturing " + video + " with id " + str(video_id))

                    # set initial amount
                    bandwidth.set_rate(str(throughput) + "kbps")

                    # create the filename
                    filename = str(video_id) + '_'
                    filename += str(throughput)

                    while not capture_movie(browser, local_ip, interface, filename, video_id, throughput):
                        time.sleep(1)
    return

if __name__ == "__main__":
    main()

import datetime
import time
import humanfriendly

from python_libs.config import StaticConfig
from python_libs.config import Inventory
from python_libs.netflix_browser import NetflixBrowser
from python_libs.adudump_capture import AdudumpCapture
from python_libs.bandwidth_manipulator import BandwidthManipulator


class Configuration:
    def __init__(self):
        self.capture_duration = 70 
        self.throughputs = [50000]
        self.wait_after_page_load = 60
        self.wait_after_throughput_adjustment = 60


static_config = StaticConfig()
# video_ids = Inventory().full_capture()
video_ids = ['70060010', '80991404']


# define the ids we want to capture
config = Configuration()

# calculate length of capture for user
loop_count = len(config.throughputs)
video_count = len(video_ids)
loop_length = config.capture_duration + config.wait_after_throughput_adjustment
full_length = video_count * (
    config.wait_after_page_load + loop_count * loop_length
)
print(
    "this capture will run for " + humanfriendly.format_timespan(full_length)
)

local_ip = static_config.local_ip
interface = static_config.network_device

# initialize the bandwidth
with BandwidthManipulator(interface, "incoming") as bandwidth:

    # initialize the browser
    with NetflixBrowser() as browser:

        for video in video_ids:
            video_id = video

            for throughput in config.throughputs:

                # inform user
                print("capturing " + video + " with id " + str(video_id))
                print("starting capture at " + str(throughput) + "kbps")
                print(
                    "waiting for " + str(config.wait_after_page_load) +
                    "s till capture starts"
                )

                # set initial amount
                bandwidth.set_rate(str(throughput) + "kbps")

                # create the filename
                filename = str(video_id) + '_'
                filename += str(throughput) + '_'
                filename += str(datetime.datetime.now().isoformat().replace(":", "_")).strip()

                # initialize the adudump capture
                with AdudumpCapture(local_ip, interface, filename) as capture:

                    print(
                        "capturing at " + str(throughput) + "kbps for " +
                        str(config.capture_duration) + "s"
                    )

                    # navigate to video
                    if not browser.navigate(video_id, throughput):
                        print("could not navigate to video")
                        continue

                    # waiting to dump
                    # time.sleep(30)



print("finished")

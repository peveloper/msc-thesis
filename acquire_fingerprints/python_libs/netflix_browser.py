import pickle
import logging
import traceback
import os
import json
import time
import psutil

from selenium import webdriver
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.remote.webelement import WebElement
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.common.by import By
from selenium.webdriver.common.action_chains import ActionChains 
from selenium.common.exceptions import TimeoutException, StaleElementReferenceException
from selenium.webdriver.common.desired_capabilities import DesiredCapabilities
from selenium.webdriver.firefox.options import Log
from browsermobproxy import Server
from urllib.parse import urlparse
from typing import Optional
from .config import StaticConfig
from .config import Inventory

config = StaticConfig()
inventory = Inventory()


class NetflixBrowser:
    __credentials = None
    __cookies = None
    __chrome = None
    # Change this parameter if autoplay is not set
    # __is_playing = True
    __is_page_loaded = False
    __server = None
    __proxy = None


    def __enter__(self):
        # resolve credentials if possible
        if os.path.isfile(config.credentials_file_path):
            self.__credentials = json.load(open(config.credentials_file_path))

        # resolve cookies if possible
        if os.path.isfile(config.cookie_file_path):
            self.__cookies = pickle.load(open(config.cookie_file_path, "rb"))

        self.__try_create_firefox()

        return self
    

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.__server.stop()
        self.__firefox.quit()

    def __try_create_firefox(self):
        """
        construct the selenium firefox browser
        """

        log = Log()
        log.level = "TRACE"

        self.__server = Server("tools/browsermob-proxy-2.1.4/bin/browsermob-proxy")
        self.__server.start()
        self.__proxy = self.__server.create_proxy()

        url = urlparse (self.__proxy.proxy).path

        # use the test video URl to try to login

        video_url = self.__get_video_url(inventory.test_video)

        options = webdriver.ChromeOptions()
        # options.add_extension(config.netflix_extension_path)
        options.add_argument('--disable-application-cache')
        options.add_argument("user-data-dir=/home/peveloper/.config/google-chrome")
        # options.add_extension(config.speedup_extension_path)
        options.add_argument("--proxy-server={0}".format(url))
        # options.add_argument('headless')


        self.__firefox = webdriver.Chrome(chrome_options=options)



        # firefox_options = Options()
        # firefox_options.headless = True
        # firefox_options.add_argument("-devtools")
        # firefox_options.add_argument(log.level)

        # firefox_profile = webdriver.FirefoxProfile(config.firefox_profile)
        # firefox_profile.set_preference("browser.link.open_newwindow", 1)
        # firefox_profile.set_preference("devtools.toolbox.selectedTool", "netmonitor")

        # self.__firefox = webdriver.Firefox(options=firefox_options, firefox_profile=firefox_profile)

        # remember to include .xpi at the end of your file names 
        # extensions = [
            # '{7be2ba16-0f1e-4d93-9ebc-5164397477a9}.xpi',
            # '{89d04aec-e93f-4f56-b77c-f2295051c13e}.xpi'
            # 'har_export_trigger-0.6.1-an+fx.xpi'
        # ]

        # for extension in extensions:

            # self.__firefox.install_addon(config.extensions_dir + "/" + extension, temporary=True)

        
        self.__firefox.get(video_url)

        # if cookies set, add them to the browser
        if self.__cookies is not None:
            for cookie in self.__cookies:
                self.__firefox.add_cookie(cookie)

        # check for the login button
        login_link = self.__try_find_element_by_class("authLinks", 2)
        if login_link is not None:
            current_url = self.__firefox.current_url
            # login button found, so we need to perform a login
            link = login_link.get_attribute("href")

            try:
                self.__firefox.get(link)
            except:
                return False

            WebDriverWait(self.__firefox, 10).until(EC.url_changes(current_url))

            # get username & password field
            username_field = self.__try_find_element_by_id("id_userLoginId")
            password_field = self.__try_find_element_by_id("id_password")

            # fill in username & password according to the credentials
            username_field.send_keys(self.__credentials["netflix"]["username"])
            password_field.send_keys(self.__credentials["netflix"]["password"])

            current_url = self.__firefox.current_url

            # submit the form
            password_field.submit()

            try:
                WebDriverWait(self.__firefox, 10).until(EC.url_changes(current_url))
            except:
                return False

            # click on the profile to be used
            self.__firefox.get("https://www.netflix.com/SwitchProfile?tkn=" + self.__credentials["netflix"]["profile"])

            # save cookies for next time
            cookies = self.__firefox.get_cookies()
            pickle.dump(cookies, open(config.cookie_file_path, "wb"))

    @staticmethod
    def __get_video_url(netflix_id: int):
        url = 'https://www.netflix.com/watch/' + str(netflix_id)
        return url

    # def rewind_movie(self, netflix_id):
        # video_url = self.__get_video_url(netflix_id)

        # self.__firefox.get(video_url)

        # print("Loading page, this may take a while ...")

        # try:
            # WebDriverWait(self.__firefox, 60).until(EC.presence_of_element_located((By.ID, "appMountPoint")))
        # except TimeoutException:
            # return False

        # if self.__try_find_element_by_class("nfp-fatal-error-view", 3) is not None:
            # title = self.__try_find_element_by_class("error-title", 3)

            # self.__firefox.save_screenshot(config.error_dir + "/" + title + ".png")

            # print("Netflix error occurred: " + title.text)
            # if title is not None:
                # if title.text == "Multiple Netflix Tabs":
                    # return False
                # if title.text == "Streaming Error":
                    # return False

        # while not self.__is_page_loaded:
            # self.__is_page_loaded = self.__get_page_status()

        # if not self.__prevent_more_requests():
            # return False

        # if not self.__seek_video():
            # return False

        # if not self.__wait_buffering():
            # return False
        
        # self.__is_page_loaded = False

        # return True

    def navigate(self, netflix_id, rate, capture):
        video_url = self.__get_video_url(netflix_id)
        current_url = self.__firefox.current_url

        print("Loading page, this may take a while ...")



        self.__firefox.get(video_url)

        # self.__prevent_more_requests()

        try:
            WebDriverWait(self.__firefox, 60).until(EC.presence_of_element_located((By.ID, "appMountPoint")))
        except TimeoutException:
            print('Page loading timeout. Exiting')
            return False



        if self.__try_find_element_by_class("nfp-fatal-error-view", 3) is not None:
            title = self.__try_find_element_by_class("error-title", 3)

            self.__firefox.save_screenshot(config.error_dir + "/" + title + ".png")

            print("Netflix error occurred: " + title.text)
            if title is not None:
                if title.text == "Multiple Netflix Tabs":
                    return False
                if title.text == "Streaming Error":
                    return False

        while not self.__is_page_loaded:
            self.__is_page_loaded = self.__get_page_status()


        if not self.__seek_video():
            return False


        self.__proxy.new_har(str(netflix_id) + "_" + str(rate), options={'captureHeaders': True})

        try:
            capture.start_adudump()
        except Exception as e:
            print(e)

        if not self.__play_video():
            return False

        
        # actions = ActionChains(self.__firefox)

        # times = int(config.speedup - 1.0) * 10
        # speedup the playback
        # for i in range(0, times):
            # actions.send_keys("d").perform()

        # actions.send_keys("r").perform()

        # if not self.__prevent_more_requests():
            # return False
        
        # if not self.__rewind():
            # return False
        # if not self.__wait_buffering():
            # return False

        # if not self.__seek_video():
            # return False
        # if not self.__wait_buffering():
            # return False

        if not self.__stop_playback():
            return False

        # time.sleep(int(config.capture_duration / config.speedup))

        if not self.__get_har(netflix_id, rate):
            return False

        self.__is_page_loaded = False
        print('DONE')

        return True

    def __get_session_summary(self):

        with open('session_summary.js', 'r') as file:
            js_script = file.read()

        try:
            summary =  self.__firefox.execute_script(js_script)
            print(summary)
        except:
            return False

        print(summary)
        
        return True

    def __stop_playback(self):

        with open('stop_video.js', 'r') as file:
            js_script = file.read()

        buffered = 0
        init = -1
        try:
            while buffered < 360000:
                buffered = self.__firefox.execute_script(js_script)
                # if buffered > 0 and init == -1:
                    # init=time.time()

                print('Buffered %d / 360000' % buffered)
        except:
            return False

        print(init)
        
        return True


    def __prevent_more_requests(self):
        """
        calls prevent_more_requests.js, block every send() method
        """

        with open('prevent_more_requests.js', 'r') as file:
            js_script = file.read()

        ready = False
        while not ready: 
            try:
                ready = self.__firefox.execute_script(js_script)
            except Exception as e:
                print(e)
                return False

        return True



    def __get_har(self, netflix_id: int, rate: int) -> Optional[bool]:
        """
        calls get_har.js and save the HAR as a JSON file

        :return True if HAR gets retrieved, False otherwise
        """

        print('Getting HARs ...')
        # with open('get_har.js', 'r') as file:
            # js_script = file.read()

        # try:
            # content = self.__firefox.execute_script(js_script)
        # except:
            # return False
        try:
            content = self.__proxy.har
        except Exception as e:
            print(e)

        filename = str(netflix_id) + "_" + str(rate) + ".har"
        print(filename)

        packets = content['log']['entries']

        entries = []
        range_list = []
        for packet in packets:
            har_entry = HarEntry()
            har_entry.url = packet["request"]["url"]
            har_entry.body_size = int(packet["response"]["bodySize"])

            if "video.net/range/" in har_entry.url and int(packet["response"]["status"]) == 200:

                # cut of url at /range to parse it
                range_url = har_entry.url[(har_entry.url.rindex("/range") + len("/range") + 1):]

                # remove query parameters
                if "?" in range_url:
                    range_url = range_url[:range_url.index("?")]

                ranges = range_url.split("-")
                har_entry.range_start = int(ranges[0])
                har_entry.range_end = int(ranges[1])

                if har_entry.range_start not in range_list:
                    har_entry.is_video = True

                # parse range (of the form 7123-8723)
                    range_list.append(har_entry.range_start)
                    entries.append(har_entry)

        with open(config.har_dir  + "/" + filename, 'w') as file:
            for entry in entries:
                if entry.body_size > 100000:
                    file.write(str(entry.get_length()) + '\t' + str(entry.range_start) + '\t' + str(entry.range_end) + '\t' + str(entry.body_size) + '\n')
            
        return True



    def __rewind(self) -> Optional[bool]:
        """
        calls rewind.js that rewinds the video

        :return the new current time in ms 
        """

        print('Rewind ...')
        with open('rewind.js', 'r') as file:
            js_script = file.read()

        try:
            self.__firefox.execute_script(js_script)
        except:
            return False

        return True 


    def __wait_buffering(self):
        """
        calls player_state.js and waits for the buffering to be complete

        """

        with open('player_state.js', 'r') as file:
            js_script = file.read()

        print('Buffering ...')
        try:
            while self.__firefox.execute_script(js_script) is not None:
                print('...')
                time.sleep(1)
        except:
            return False

        print('\n')
            
        return True


    def __seek_video(self) -> Optional[int]:
        """
        calls seek_video.js that seeks the playback forward

        :return the new current time in ms 
        """

        print('Seeking ...')
        with open('seek_video.js', 'r') as file:
            js_script = file.read()


        try:
            self.__firefox.execute_script(js_script)
        except Exception as e:
            print(e)
            return False

        return True


    def __play_video(self):

        filename = 'play_video.js'

        with open(filename, 'r') as file:
            js_script = file.read()
        try:
            self.__firefox.execute_script(js_script)
        except:
            return False

        return True


    def __toggle_video_playback(self) -> Optional[bool]:
        """
        calls either play.js or pause.js 

        :param play: True for play, False for pause
        :return True if succesfull
        """

        filename = 'pause_video.js'

        print('Starting playback ...')
        with open(filename, 'r') as file:
            js_script = file.read()
        try:
            self.__firefox.execute_script(js_script)
        except:
            return False

        return True

    def __get_page_status(self) -> Optional[bool]:
        """
        calls page_status.js that check if the player is loaded

        :return True if the player is loaded, False otherwise
        """

        with open('page_status.js', 'r') as file:
            js_script = file.read()

        print('Loading Netflix video player ...')
        while not self.__is_page_loaded:
            # js boolean gets casted into python's bool
            try:
                self.__is_page_loaded = self.__firefox.execute_script(js_script)
            except:
                return False

            time.sleep(1)

        print('Player is ready!')
        return True


    def __try_find_element_by_id(self, css_id: str, retries: int = 5) -> Optional[WebElement]:
        """
        looks in the chrome instance if it can find that element
        retries as long as specified, waits 1 seconds before trying again

        :param css_id: the id of the css element
        :param retries: the amount of retries to perform
        :return: the selenium object if found, else None
        """
        while retries > 0:
            try:
                return self.__firefox.find_element_by_id(css_id)
            except:
                # don't care, just retry
                time.sleep(1)
                retries -= 1

        return None

    def __try_find_element_by_class(self, css_class: str, retries: int = 5) -> Optional[WebElement]:
        """
        looks in the chrome instance if it can find that element
        retries as long as specified, waits 1 seconds before trying again

        :param css_class: the class of the css element
        :param retries: the amount of retries to perform
        :return: the selenium object if found, else None
        """

        while retries > 0:
            try:
                return self.__firefox.find_element_by_class_name(css_class)
            except:
                # don't care, just retry
                time.sleep(1)
                retries -= 1

        return None

    @staticmethod
    def __try_with_repeat(func, retries: int):
        """
        retries a function multiple times, returns None if all fail
        waits 1 second before each retry

        :param func: the function to try repeatedly
        :param retries: the amount of retries to try
        :return:
        """
        while retries > 0:
            try:
                return func()
            except:
                # don't care, just retry
                time.sleep(1)
                retries -= 1
        return None

class HarEntry:

    def __init__(self):
        self.url = None
        self.body_size = None
        self.is_video = False
        self.range_start = None
        self.range_end = None

    def __repr__(self):
        return self.__dict__.__repr__()

    def get_length(self):
        return self.range_end - self.range_start

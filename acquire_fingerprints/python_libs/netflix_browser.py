import pickle
import os
import json
import time
from selenium import webdriver
from selenium.webdriver.firefox.options import Options
from selenium.webdriver.remote.webelement import WebElement
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.common.keys import Keys
from selenium.webdriver.common.by import By
from selenium.webdriver.common.action_chains import ActionChains 
from selenium.common.exceptions import TimeoutException, StaleElementReferenceException
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
    __is_playing = True
    __is_page_loaded = False

    def __enter__(self):
        # resolve credentials if possible
        if os.path.isfile(config.credentials_file_path):
            self.__credentials = json.load(open(config.credentials_file_path))

        # resolve cookies if possible
        if os.path.isfile(config.cookie_file_path):
            self.__cookies = pickle.load(open(config.cookie_file_path, "rb"))

        # finally start chrome
        self.__try_create_firefox()

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        # closes the chrome browser
        # self.__firefox.close()
        self.__firefox.quit()

    def __try_create_firefox(self):
        """
        construct the selenium firefox browser
        """

        # use the test video URl to try to login
        video_url = self.__get_video_url(inventory.test_video)

        firefox_options = Options()
        firefox_options.headless = True
        # firefox_options.log.level = "trace"

        firefox_profile = webdriver.FirefoxProfile(config.firefox_profile)
        # firefox_profile = webdriver.FirefoxProfile()
        firefox_profile.set_preference("browser.link.open_newwindow", 1)

        self.__firefox = webdriver.Firefox(options=firefox_options, firefox_profile=firefox_profile)

        # remember to include .xpi at the end of your file names 
        extensions = [
            '{7be2ba16-0f1e-4d93-9ebc-5164397477a9}.xpi',
            '{89d04aec-e93f-4f56-b77c-f2295051c13e}.xpi'
        ]

        for extension in extensions:
            self.__firefox.install_addon(config.extensions_path + "/" + extension, temporary=True)
        # extension_path = '/home/ubuntu/extension/{89d04aec-e93f-4f56-b77c-f2295051c13e}.xpi'
        
        # firefox_profile.add_extension(extension="/home/ubuntu/.mozilla/firefox/gexhyx73.default/extensions/{89d04aec-e93f-4f56-b77c-f2295051c13e}.xpi")


        # construct chrome & request the test url
        # self.__firefox.install_addon(extension_path, temporary=True)

        self.__firefox.get(video_url)

        # if cookies set, add them to the browser
        if self.__cookies is not None:
            for cookie in self.__cookies:
                self.__firefox.add_cookie(cookie)

        # check for the login button
        login_link = self.__try_find_element_by_class("authLinks", 2)
        if login_link is not None:

            # login button found, so we need to perform a login
            link = login_link.get_attribute("href")
            print(link)
            self.__firefox.get(link)

            time.sleep(3)

            # get username & password field
            username_field = self.__try_find_element_by_id("id_userLoginId")
            password_field = self.__try_find_element_by_id("id_password")

            # fill in username & password according to the credentials
            username_field.send_keys(self.__credentials["netflix"]["username"])
            password_field.send_keys(self.__credentials["netflix"]["password"])

            current_url = self.__firefox.current_url
        # self.throughputs = [50000]

            # submit the form
            password_field.submit()

            WebDriverWait(self.__firefox, 5).until(EC.url_changes(current_url))

            self.__firefox.save_screenshot('profile.png')

            print("https://www.netflix.com/SwitchProfile?tkn="+ self.__credentials["netflix"]["profile"])

            # click on the profile to be used
            self.__firefox.get("https://www.netflix.com/SwitchProfile?tkn=" + self.__credentials["netflix"]["profile"])

            # save cookies for next time
            cookies = self.__firefox.get_cookies()
            pickle.dump(cookies, open(config.cookie_file_path, "wb"))

    @staticmethod
    def __get_video_url(netflix_id: int, rate: int = None):
        url = 'https://www.netflix.com/watch/' + str(netflix_id)
        if rate is not None:
            url += '?rate=' + str(rate)

        return url

    def navigate(self, netflix_id: int, rate: int) -> bool:
        video_url = self.__get_video_url(netflix_id)
        current_url = self.__firefox.current_url

        print(current_url)

        self.__firefox.get(video_url)
        print(self.__firefox.current_url)

        try:
            WebDriverWait(self.__firefox, 60).until(EC.presence_of_element_located((By.ID, "appMountPoint")))
        except TimeoutException:
            print('TIMEOUT')

        if self.__try_find_element_by_class("nfp-fatal-error-view", 3) is not None:
            title = self.__try_find_element_by_class("error-title", 3)
            self.__firefox.save_screenshot('error.png')
            print("Netflix error occurred: " + title.text)
            if title is not None:
                if title.text == "Multiple Netflix Tabs":
                    # this error is critical, netflix won't allow us to continue capturing
                    # user may needs to reboot the computer for it to work again
                    print("aborting; consider rebooting the computer for the error to go away")
                    return False
                if title.text == "Unexpected Error":
                    # this error happens when javascript selects a profile unsupported by this video type
                    # this happens often, and is no reason to stop capturing
                    return False
                if title.text == "Streaming Error":
                    return False
                print("halting; new error found!")
                time.sleep(86400)
            else:
                # unknown error occurred; per default we continue
                return True


        while not self.__is_page_loaded:
            self.__is_page_loaded = self.__get_page_status()

        self.__rewind()
        self.__wait_buffering()
        self.__seek_video()
        self.__wait_buffering()
        # self.__firefox.save_screenshot(config.screenshots_dir + "/" + str(netflix_id) + "_" + str(rate) + "_1.png")
        time.sleep(120)
        self.__toggle_video_playback(False)
        print(self.__is_playing)

        return True

    def __rewind(self) -> Optional[int]:
        """
        calls rewind.js that rewinds the video

        :return the new current time in ms 
        """

        with open('seek_video.js', 'r') as file:
            js_script = file.read()

        return self.__firefox.execute_script(js_script)



    def __wait_buffering(self):
        """
        calls player_state.js and waits for the buffering to be complete

        """

        with open('player_state.js', 'r') as file:
            js_script = file.read()

        while self.__firefox.execute_script(js_script) is not None:
            print('...')
            time.sleep(1)
        print('\n')
            
        return


    def __seek_video(self) -> Optional[int]:
        """
        calls seek_video.js that seeks the playback forward

        :return the new current time in ms 
        """

        with open('seek_video.js', 'r') as file:
            js_script = file.read()

        return self.__firefox.execute_script(js_script)


    def __toggle_video_playback(self, play: bool) -> Optional[bool]:
        """
        calls either play.js or pause.js 

        :param play: True for play, False for pause
        :return True if playing, False otherwise
        """

        if play:
            filename = 'play_video.js'
            print('Starting playback ...')
        else:
            filename = 'pause_video.js'
            print('Pausing playback ...')

        with open(filename, 'r') as file:
            js_script = file.read()
        self.__is_playing = self.__firefox.execute_script(js_script)

        return self.__is_playing

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
            self.__is_page_loaded = self.__firefox.execute_script(js_script)
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

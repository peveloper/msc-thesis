import os


class StaticConfig:
    def __init__(self):
        self.root_dir = os.path.abspath(os.path.dirname("."))

        self.skip_seconds = 120
        self.wait_seconds = 4

        self.capture_version = 1
        self.attack_version = 1

        self.analyze_dir = self.root_dir + "/" + "analyze"

        self.adudump_dir = self.root_dir + "/" + "tools/adudump"

        self.config_dir = self.root_dir + "/" + "config"
        self.temp_dir = self.root_dir + "/" + "temp"

        self.attack_dir = self.root_dir + "/" + "attack"

        self.plot_dir = self.root_dir + "/" + "plot"
        self.log_dir = self.root_dir + "/" + "log"
        self.error_dir = self.root_dir + "/" + "error"
        self.screenshots_dir = self.root_dir + "/" + "screenshots"

        self.firefox_profile = "/home/ubuntu/.mozilla/firefox/t22whui4.test"
        self.extensions_path = self.root_dir + "/" + "tools"
        self.credentials_file_path = self.config_dir + "/" + "credentials.json"
        self.cookie_file_path = self.temp_dir + "/" + "cookies.pkl"

        self.speedup = 1.8

        self.local_ip = "172.31.21.190/20"
        self.network_device = "eth0"


class Inventory:
    def __init__(self):
        # netflix test video
        self.test_video = 80018499

        # action
        self.transformers = 70103763
        self.civil_war = 80088567

        # anime
        self.naruto = 70105699
        self.berserk = 70276596

        # documentaries
        self.minimalism = 80114460
        self.expedition_happiness = 80225528

        # cartoon
        self.family_guy = 80111454
        self.bojack_horseman = 70298930

    def full_capture(self):
        return self.__dict__

    def small_capture(self):
        return {"bojack": self.bojack_horseman, "family": self.family_guy}

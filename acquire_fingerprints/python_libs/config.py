import os
import csv


class StaticConfig:
    def __init__(self):
        self.root_dir = os.path.abspath(os.path.dirname("."))

        self.capture_duration = 120
        self.wait_after_throughput_adjustment = 20
        # self.throughputs = [800, 1000, 2000, 3500, 4000, 5000, 10000]
        # self.throughputs = [800, 1200, 2000, 3500, 4200, 4800, 5500, 6500, 7050, 10000, 15000, 20000]
        self.throughputs = [20000]

        self.adudump_dir = self.root_dir + "/" + "tools/adudump"
        self.config_dir = self.root_dir + "/" + "config"
        self.temp_dir = self.root_dir + "/" + "temp"
        self.log_dir = self.root_dir + "/" + "log"
        self.har_dir = self.root_dir + "/" + "har"
        self.error_dir = self.root_dir + "/" + "error"
        self.screenshots_dir = self.root_dir + "/" + "screenshots"
        self.titles_dir = self.root_dir + "/" + "netflix_titles"
        self.extensions_dir = self.root_dir + "/" + "tools"
        self.netflix_extension_path = self.root_dir + "/" + "tools/Netflix-1080p_v1.13.crx"

        self.firefox_profile = self.root_dir + "/firefox_profile/cuyp2h9p.netflix"
        self.credentials_file_path = self.config_dir + "/" + "credentials.json"
        self.cookie_file_path = self.temp_dir + "/" + "cookies.pkl"
        self.selected_genres = [
            'Comedies',
            'Thrillers',
            'Sci-Fi & Fantasy',
            'Documentaries',
            'Movies for ages 8 to 10'
        ]
        # self.movies_per_genre = 5

        self.speedup = 1.0

        self.local_ip = "192.168.0.157/24"
        self.network_device = "enp3s0"


class Inventory:
    def __init__(self):
        # netflix test video
        self.test_video = 80018499
        self.config = StaticConfig()

    def get_genres(self):
        self.genres = set()
        for id in self.movies_by_id.keys():
            self.genres.add(self.movies_by_id[id])
        return 

    def get_selection_of_movies_by_genre(self):
        self.movies_selection = []

        self.selected_genres = self.config.selected_genres 

        for genre in self.selected_genres:
            for x in range(0, len(self.movies_by_genre[genre])):
                self.movies_selection.append(self.movies_by_genre[genre][x])

        return self.movies_selection

    def get_movies(self):
        self.movies_by_id = {}
        self.movies_by_genre = {}
        with open(self.config.titles_dir + "/" + "titles.csv" , mode='r') as csv_file:
            csv_reader = csv.DictReader(csv_file)
            line_count = 0
            for row in csv_reader:
                id = row['id']
                genre = row['genre']
                title = row['title']

                if line_count == 0:
                    line_count += 1
                if row["id"] not in self.movies_by_id:
                    # add movie
                    self.movies_by_id[id] = genre
                    try:
                        self.movies_by_genre[genre].append(id)
                    except KeyError:
                        self.movies_by_genre[genre] = []
                line_count += 1
        return self.movies_by_id

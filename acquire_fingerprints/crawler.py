import scrapy
from scrapy.http import Request


class netflixSpider(scrapy.Spider):
    name = 'netflix_spider'
    start_urls = ['https://www.netflix.com/ch-en/login']
    visitedLinks = start_urls

    def parse(self, response):
        return [scrapy.FormRequest.from_response(response,
                    formdata={'username': 'pstefanoethz@gmail.com', 'password': 'hx2kBicEyuAN'},
                    callback=self.after_login)]

    def after_login(self, response):
        print(response.body)
        # check login succeed before going on
        if "authentication failed" in response.text:
            self.log("Login failed", level=log.ERROR)
            return
        # We've successfully authenticated, let's have some fun!
        else:
            return Request(url="http://www.netflix.com/ch-en/browse/genre/34399?so=az",
               callback=self.parse_movies)

    def parse_movies(self, response):
        sections = '.nm-collections-row'
        movies = '.nm-content-horizontal-row-item'
        pageValue = response.css('.nm-collections-header-name::text').extract_first()

        for section in response.css(sections):
            pageLink = section.css('.nm-collections-link::attr(href)').extract_first()

            for movie in section.css(movies):
                movieLink = movie.css('.nm-collections-link::attr(href)').extract_first()
                movieId = movieLink.split('/')[-1]
                genreValue = section.css('.nm-collections-row-name::text').extract_first()
                titleValue = movie.css('a span::text').extract_first()

                yield {
                    'id': movieId,
                    'genre': genreValue,
                    'title': titleValue
                }

            if pageLink and pageLink not in self.visitedLinks:
                self.visitedLinks.append(pageLink)

                yield scrapy.Request(
                    response.urljoin(pageLink),
                    callback=self.parse_movies
                )

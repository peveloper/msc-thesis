from browsermobproxy import Server
from selenium.webdriver.common.desired_capabilities import DesiredCapabilities
from selenium import webdriver

desired_capabilities = DesiredCapabilities.FIREFOX.copy()
desired_capabilities['acceptInsecureCerts'] = True

# server = Server("browsermob-proxy-2.1.4/bin/browsermob-proxy")
# server.start()
# proxy = server.create_proxy()

profile  = webdriver.FirefoxProfile("/home/peveloper/.mozilla/firefox/gexhyx73.default")
# profile.set_proxy(proxy.selenium_proxy())
driver = webdriver.Firefox(firefox_profile=profile, capabilities=desired_capabilities)


# proxy.new_har("nf")
driver.get("https://www.netflix.com")
# proxy.har # returns a HAR JSON blob

# server.stop()
driver.quit()

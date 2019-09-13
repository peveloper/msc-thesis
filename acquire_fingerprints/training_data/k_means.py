from pandas import DataFrame
import matplotlib.pyplot as plt
from sklearn.cluster import KMeans
from matplotlib.pyplot import figure

import sys
import pandas as pd
import io
import os

csv = ""

for line in sys.stdin:
    csv += line

df = pd.read_csv(io.StringIO(csv), sep='&', header=None, names=['A', 'B', 'C', 'D', 'E'])

movie_ID = df['A']
del df['A']

kmeans = KMeans(n_clusters=10).fit(df)
centroids = kmeans.cluster_centers_

crawled_titles = pd.read_csv(os.path.join(os.path.dirname(__file__),"../netflix_titles/titles.csv"), header=None, names=['id','genre', 'title'], index_col=0)
crawled_titles.drop_duplicates(['title'], keep='first', inplace=True)
crawled_titles.to_string

with pd.option_context('display.max_rows', None, 'display.max_columns', None):  # more options can be specified also
    print(crawled_titles)

print(crawled_titles['title'].loc['1151721'])

titles={}
for ID in movie_ID:
    print(ID)
    titles[ID] = crawled_titles['title'].loc[str(ID)]

for id in titles.keys():
    print(id, titles[id])

l = [i for i in range(100)]
fig, ax = plt.subplots()
figure(num=1, figsize=(48, 48))


ax.scatter(l, df['B'], c= kmeans.labels_.astype(float), s=25, alpha=0.5)
for i, k in enumerate(titles.keys()):
    ax.annotate(titles[k], (l[i], df['B'][i]))
plt.show(figure(num=1))

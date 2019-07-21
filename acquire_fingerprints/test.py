import numpy as np
import scipy.fftpack
from scipy.signal import savgol_filter
import matplotlib
matplotlib.use('GTK3Agg',warn=False, force=True)
import matplotlib.pyplot as plt

files = ['data/1179080_800.dat', 'data/1179080_1200.dat',
        'data/1179080_2000.dat', 'data/1179080_3500.dat',
        'data/1179080_4200.dat', 'data/1179080_6500.dat',
        'data/1179080_7050.dat', 'data/1179080_10000.dat',
        'data/1179080_15000.dat', 'data/1179080_20000.dat',
        'data/81113921_2000.dat', 'data/81113921_3500.dat',
        'data/81113921_4200.dat', 'data/81113921_6500.dat',
        'data/81113921_7050.dat', 'data/81113921_10000.dat']

Xs = []
Ys = []

for i,file in enumerate(files[:10]):
    bitrate = int(file.split('_')[-1].split('.')[0])
    arr = np.loadtxt(file, delimiter='\t', usecols=1)
    arr =(arr - np.mean(arr)) / (100000)
    plt.plot(arr)
    plt.show()
    # print(arr)
    w = int(0.2 * len(arr))
    if len(arr) > 200:
        w = int(0.5 * len(arr))
    if w % 2 == 0:
        w+=1
    arr = savgol_filter(arr, w, 2)

    Ys.append(arr)
    plt.plot(arr)
    plt.savefig('plot%d.png' % i)
    plt.show()

    # print(arr)

fig, ax = plt.subplots()
toplot=[]
for i, F in enumerate(Ys):
    toplot.append((F))
print(toplot)
Y = np.mean(np.stack(toplot, axis=0), axis=1)

print(Y)
print(Y.shape)
ax.plot(Y, label='Y')
ax.legend()
    
# plt.show()
fig.savefig('agg.png')
# plt.plot(scipy.signal.correlate(Ys[4], Ys[5], mode='same'))
# plt.show()
# plt.plot(scipy.signal.correlate(Ys[5], Ys[4], mode='same'))
# plt.show()

# print(np.argmax(scipy.signal.correlate(Ys[4], Ys[5], mode='same')))
# print(np.argmax(scipy.signal.correlate(Ys[5], Ys[4], mode='same')))
# print((scipy.signal.correlate(Ys[1], Ys[0])))

# print(len(Ys[0]))
# print(Ys[1][90:])



# print(np.correlate(Ys[0], Ys[1], mode='full'))

# for y in Ys:
    # N = len(y)
    # T = 2 * N
    # x = np.linspace(0.0, N*T, N)
    # yf = scipy.fftpack.fft(y)
    # print(yf)
    # F = np.zeros(N)
    # F = F.tolist()
    # F[0] = yf[0]
    # F[1] = yf[1]
    # F[2] = yf[2]
    # F[10] = yf[10]
    # F[20] = yf[20]
    # F[25] = yf[25]
    # F[26] = yf[26]
    # F[29] = yf[29]
    
    # xf = np.linspace(0.0, 1.0/(2.0*T), N/2)
    # z = scipy.fftpack.ifft(F)
    # print(z)

    # fig, ax = plt.subplots()
    # ax.plot(xf, 2.0/N * np.abs(yf[:N//2]))
    # plt.show() 
    # fig1, ax1 = plt.subplots()
    # ax1.plot(xf, np.abs(F[:N//2]))
    # plt.show() 

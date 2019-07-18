import numpy as np
import scipy.fftpack
import matplotlib
matplotlib.use('GTK3Agg',warn=False, force=True)
import matplotlib.pyplot as plt

files = ['data/1179080_10000.dat', 'data/1179080_3500.dat', 'data/1179080_2000.dat']

Xs = []
Ys = []

for file in files:
    arr = np.loadtxt(file, delimiter='\t', usecols=1)
    arr =(arr - np.mean(arr)) / (np.std(arr) * len(arr))

    Ys.append(arr)

    print(arr)

print(np.correlate(Ys[0], Ys[1], mode='full'))

for filename in files:
    y = []
    with open(filename, 'r') as f:
        lines = f.readlines()

        for line in lines:
            y.append(line.split()[-1])

    print(y)

    # Number of samplepoints
    N = len(y)
    # sample spacing
    T = 2 * N
    x = np.linspace(0.0, N*T, N)
    yf = scipy.fftpack.fft(y)
    print(yf)
    F = np.zeros(N)
    F = F.tolist()
    F[0] = yf[0]
    F[1] = yf[1]
    F[2] = yf[2]
    F[10] = yf[10]
    F[20] = yf[20]
    F[25] = yf[25]
    F[26] = yf[26]
    F[30] = yf[30]
    
    xf = np.linspace(0.0, 1.0/(2.0*T), N/2)
    z = scipy.fftpack.ifft(F)
    print(z)

    fig, ax = plt.subplots()
    ax.plot(xf, 2.0/N * np.abs(yf[:N//2]))
    plt.show() 
    fig1, ax1 = plt.subplots()
    ax1.plot(xf, np.abs(F[:N//2]))
    plt.show() 

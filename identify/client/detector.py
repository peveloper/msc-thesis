import sys
import time
from socket import *
from collections import deque
from collections import OrderedDict

class Tracker:
  def __init__(self, ipPair, timestamp, windowSize):
    self.addresses = ipPair
    self.startTime = int(timestamp)
    self.lastTime = timestamp
    self.window = deque([],windowSize)
    self.identified = False
    self.videoTitle = ""
    self.index = -1
    self.current_index = -1
    
  def addADU(self, timestamp, size, clientSocket, idx):
    self.lastTime = timestamp
    self.window.append(size)

    if (len(self.window) == windowSize) and (not self.identified):
      output = str(self.window[0])
      for x in range(1,windowSize):
        output += "," + str(self.window[x])
      clientSocket.send(output + "\n")

      result = clientSocket.recv(1024)
      if result != "none\n":
        if not self.identified:
          tokens = result.split("\t")
          self.identified = True
          self.videoTitle = tokens[0]
          self.bitrate = tokens[1]
          self.index = tokens[2]
          self.current_index = idx + 1 - windowSize

    
  def getLastTime(self):
    return self.lastTime

  def printVideo(self):
    if self.identified:
      sys.stdout.write(self.videoTitle + "\t" + self.bitrate + "\t" + self.index + "\t" + str(self.current_index) + "\n")

### Main Program ###
start_time = time.time()

serverName = sys.argv[1]
serverPort = int(sys.argv[2])
windowSize = int(sys.argv[3])

clientSocket = socket(AF_INET, SOCK_STREAM)
clientSocket.connect((serverName,serverPort))

cnxTracker = OrderedDict()

for i, adu in enumerate(sys.stdin):
  adu = adu.rsplit('\n') # remove trailing newline
  fields = adu[0].split()

  try:
    currentTime = float(fields[0])
    cnxKey = fields[1].split('>')[0].split('.')[0]
    size = int(fields[2])
  except ValueError:
    continue

  if cnxKey in cnxTracker:
    cnx = cnxTracker[cnxKey]
    del cnxTracker[cnxKey]
  else:
    cnx = Tracker(cnxKey, currentTime, windowSize)


  cnx.addADU(currentTime, size, clientSocket, i)    

  cnxTracker[cnxKey] = cnx

  deleteList = []
  for k,v in cnxTracker.iteritems():
    if ((currentTime - v.getLastTime()) > windowSize * 4):
      deleteList.append(k)
    else:
      break
  for key in deleteList:
    cnxTracker[key].printVideo()
    del cnxTracker[key]
  
clientSocket.send("complete\n")
clientSocket.close()

for k,v in cnxTracker.iteritems():
  v.printVideo()

print(time.time() - start_time)


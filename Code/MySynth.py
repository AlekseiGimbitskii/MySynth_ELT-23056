import pyaudio
import time
from pysndfx import AudioEffectsChain
import numpy as np

#-----------------configure audio device-----------
FORMAT = pyaudio.paInt16
CHANNELS = 2
RATE = 44100
CHUNK = 1024

audio = pyaudio.PyAudio()

stream = audio.open(format=FORMAT, channels=CHANNELS,
                rate=RATE, input=True,
                frames_per_buffer=CHUNK,
                output=True)

while (1):
    start = time.time()
    data = stream.read(CHUNK)
    stopRec = time.time()
    print("time for recording: {}".format(stopRec-start))
    stream.write(data)
    stopPlay = time.time()
    print("time for playing: {}".format(stopPlay-stopRec))
    print("latency: {}".format(stopPlay-start))



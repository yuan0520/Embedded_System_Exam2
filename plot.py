import matplotlib.pyplot as plt
import numpy as np
import serial
import time

serdev = '/dev/ttyACM0'
s = serial.Serial(serdev)

n = 10
seq = np.arange(n)

feature = []
ID = []

for i in range(n):
    line = s.readline()
    print("line: ", line)
    ges_fea = line.split(",")
    ID_i = int(ges_fea[0].split(":")[-1])
    print("ID_i: {}".format(ID_i))
    ID.append(ID_i)
    fea_i = int(ges_fea[1].split(":")[-1])
    print("fea_i: {}".format(fea_i))
    feature.append(fea_i)
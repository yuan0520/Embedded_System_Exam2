import paho.mqtt.client as paho
import time
import serial

serdev = '/dev/ttyACM0'
s = serial.Serial(serdev, 9600)

# https://os.mbed.com/teams/mqtt/wiki/Using-MQTT#python-client

# MQTT broker hosted on local machine
mqttc = paho.Client()

# Settings for connection
# TODO: revise host to your IP
host = "172.20.10.10"
topic1 = "angle_sel"
# topic2 = "angle_det"

# Callbacks
def on_connect(self, mosq, obj, rc):
    print("Connected rc: " + str(rc))

def on_message(mosq, obj, msg):
    m = str(msg.payload)
    print(m)
    num_s = m.split(":")[-1]
    num = int(num_s.split('.')[0])
    if num == 10:
        print("We have already detected for 10 times, go back.")
        s.write(bytes("/back_finished/run\r\n", 'UTF-8'))
    


def on_subscribe(mosq, obj, mid, granted_qos):
    print("Subscribed OK")

def on_unsubscribe(mosq, obj, mid, granted_qos):
    print("Unsubscribed OK")

# Set callbacks
mqttc.on_message = on_message
mqttc.on_connect = on_connect
mqttc.on_subscribe = on_subscribe
mqttc.on_unsubscribe = on_unsubscribe

# Connect and subscribe
print("Connecting to " + host + "/" + topic1)
mqttc.connect(host, port=1883, keepalive=60)
mqttc.subscribe(topic1, 0)

# Loop forever, receiving messages
mqttc.loop_forever()
import socket
import datetime

# socket-test
# Acts like a displayboard in that it acts as a server accepting connections from RTPro
# and simply echoes what it receives to stdout for analysis purposes.
# In the RTPro, open a race, go to settings tab, Displayboard, Displayboard List, Add an ALGE with 1 panel and Running Time mode.
# Initially use the address as 0 = TDC4000 (higher numbers simply mean that the RTPro will send the TDC4000 value AND will send 
# messages for to incremental board letters A, B, ... until reaching the address provided for this display.)
# On the ALGE row, set the IP address to be the computer that is running this emulator (py sockettest.py)

TCP_IP = '0.0.0.0' # monitor all available addresses
TCP_PORT = 21967
BUFFER_SIZE = 50  # "normally 1024" but can reduce to get faster response

print("Setting up socket...")
myServerSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# even so, may have to wait for TIME_WAIT timeout (1min30sec?) after kill before can reopen)
myServerSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) 
myServerSocket.bind((TCP_IP, TCP_PORT))
myServerSocket.listen(1)

print("Waiting for TCP connection...")
conn, addr = myServerSocket.accept()
print("Connection address:", addr)

try:
	while 1:
		data = conn.recv(BUFFER_SIZE)
		if not data: break
		print(datetime.datetime.now().time(), " Received(len=", len(data), "):", data)
		#conn.send(data)  # would echo data back
except KeyboardInterrupt:
	conn.close()
	print("Socket closed")

# test client side:
# TCP_IP = '192.168.1.10'
# TCP_PORT = 21967
# BUFFER_SIZE = 1024
# MESSAGE = "Hello123"
# s=socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# s.connect((TCP_IP, TCP_PORT))
# s.send(MESSAGE)
# data = s.recv(BUFFER_SIZE)
# s.close()
# print("Received data:", data)

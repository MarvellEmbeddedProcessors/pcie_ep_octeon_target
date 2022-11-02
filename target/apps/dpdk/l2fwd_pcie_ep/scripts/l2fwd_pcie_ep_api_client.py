import socket
import sys
import json
import time

HOST, PORT = "localhost", 8888

setState0 = {
		"jsonrpc": "2.0",
		"id": 1,
		"method": "mrvl_l2fwd_set_fwd_state",
		"params": {
			"state": 0
		}
}

delPair = {
		"jsonrpc": "2.0",
		"id": 2,
		"method": "mrvl_l2fwd_del_fwd_pair",
		"params": {
			"port1": "0002:12:00.1",
			"port2": "0002:02:00.0"
		}
}

addPair = {
		"jsonrpc": "2.0",
		"id": 3,
		"method": "mrvl_l2fwd_add_fwd_pair",
		"params": {
			"port1": "0002:12:00.1",
			"port2": "0002:02:00.0"
		}
}

setState1 = {
		"jsonrpc": "2.0",
		"id": 4,
		"method": "mrvl_l2fwd_set_fwd_state",
		"params": {
			"state": 1
		}
}


# Create a socket (SOCK_STREAM means a TCP socket)
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

try:
	# Connect to server and send data
	sock.connect((HOST, PORT))

	data = json.dumps(setState0)
	sock.sendall(bytes(data, encoding="utf-8"))
	received = sock.recv(1024)
	received = received.decode("utf-8")
	print("Sent:     {}".format(data))
	print("Received: {}".format(received))

	data = json.dumps(delPair)
	sock.sendall(bytes(data, encoding="utf-8"))
	received = sock.recv(1024)
	received = received.decode("utf-8")
	print("Sent:     {}".format(data))
	print("Received: {}".format(received))

	data = json.dumps(addPair)
	sock.sendall(bytes(data, encoding="utf-8"))
	received = sock.recv(1024)
	received = received.decode("utf-8")
	print("Sent:     {}".format(data))
	print("Received: {}".format(received))

	time.sleep(5)

	data = json.dumps(setState1)
	sock.sendall(bytes(data, encoding="utf-8"))
	received = sock.recv(1024)
	received = received.decode("utf-8")
	print("Sent:     {}".format(data))
	print("Received: {}".format(received))

finally:
	sock.close()


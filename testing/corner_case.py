#!/usr/bin/python3

import socket
import os

# N characters in custom_put
custom_put = ("PUT /hello_file HTTP/1.1\r\nContent-Length: 5\r\n\r\n"
              "hello")

HOST = "localhost"
PORT = 8888
chunks = 5

if __name__ == "__main__":
   # https://realpython.com/python-sockets/
   with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as so:
      so.connect((HOST, PORT))
      for idx in range(0, chunks):
         start_char = int(idx * (len(custom_put) / chunks))
         end_char = int((idx+1) * (len(custom_put) / chunks))
         if (idx + 1 == chunks):
            end_char = int(len(custom_put))
         
         msg = custom_put[start_char:end_char]
         #print(msg)
         so.sendall(msg.encode())
      data = so.recv(1024)
      print(data.decode())

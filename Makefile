CC = gcc
CFLAGS = -Wall -O2

all: server client http_server

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

http_server: http_server.c
	$(CC) $(CFLAGS) -o http_server http_server.c

clean:
	rm -f server client http_server

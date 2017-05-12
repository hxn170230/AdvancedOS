all: server client
server: server.o
	gcc -o server server.o
server.o: server.c server.h channel.h object.h
	gcc -c -Wall server.c -I.
client: client.o
	gcc -o client client.o
client.o: client.c client.h channel.h object.h
	gcc -c -Wall client.c -I.

clean:
	rm -rf client.o server.o

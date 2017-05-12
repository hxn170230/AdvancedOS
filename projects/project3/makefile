server: server.o
	gcc -o server server.o
server.o: server.c server.h
	gcc -c -Wall server.c -I.

clean:
	rm -rf server.o server

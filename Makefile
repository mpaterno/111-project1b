#NAME: Matthew Paterno
#ID: 904756085
#EMAIL: mpaterno@g.ucla.edu

.SILENT:

default: client server

client: 
	gcc -g -Wall -Wextra -o lab1b-client lab1b-client.c
server: 
	gcc -g -Wall -Wextra -o lab1b-server lab1b-server.c

clean: 
	rm -f lab1b-client lab1b-server *.o *.txt *.tar.gz

dist:
	tar -czvf lab1b-904756085.tar.gz Makefile README lab1b-server.c lab1b-client.c
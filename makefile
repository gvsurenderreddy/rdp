all: rwss rwsc

rwss: rdp.o rwss.o
	gcc rdp.o rwss.o -o rwss

rwsc: rdp.o rwsc.o
	gcc rdp.o rwsc.o -o rwsc

rwss.o: rwss.c
	gcc -c rwss.c

rwsc.o: rwsc.c
	gcc -c rwsc.c

rdp.o: rdp.c rdp.h
	gcc -c rdp.c

clean:
	rm -rf *o rwss rwsc

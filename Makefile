all: meltdown.o
	gcc meltdown.o -o meltdown
meltdown.o: meltdown.c
	gcc -march=native -c meltdown.c
clean:
	rm meltdown
	rm meltdown.o

CC=gcc
CFLAGS=-ggdb3

OBJ = crc32.o savetool.o

savetool: $(OBJ)
	$(CC) -o savetool $^

clean:
	rm -rf netsim $(OBJ)

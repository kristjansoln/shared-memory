INCLUDES=-I "include"
CC=gcc
LIBS =
CFLAGS = -g  -Wall
OBJS = main.o
BIN_NAME = pipe

# Add additional search paths
VPATH = src

$(BIN_NAME): ${OBJS}
	${CC} ${CFLAGS} ${INCLUDES} -o $@ ${OBJS} ${LIBS}

%.o: %.c
	${CC} ${CFLAGS} ${INCLUDES} -c $<

.PHONY: clean

clean:
	rm -r *.o

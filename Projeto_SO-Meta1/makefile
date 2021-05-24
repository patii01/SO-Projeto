# Duarte Emanuel Ramos Meneses, 2019216949
# Patricia Beatriz Silva Costa, 2019213995

CC=gcc
FLAGS=-Wall -g -pthread
PROG=test
OBJS=projeto.o

all:	${PROG}

clean:
	rm ${OBJS} *~ ${PROG}

${PROG}:	${OBJS}
	${CC} ${FLAGS} ${OBJS} -lm -o $@

.c.o:
	${CC} ${FLAGS} $< -c -o $@

##########################
projeto.o:	projeto.c


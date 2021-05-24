# Duarte Emanuel Ramos Meneses, 2019216949
# Patricia Beatriz Silva Costa, 2019213995

CC=gcc
FLAGS=-Wall -g -pthread
PROG=test_3
OBJS=projeto_NOVO1.o

all:	${PROG}

clean:
	rm ${OBJS} *~ ${PROG}

${PROG}:	${OBJS}
	${CC} ${FLAGS} ${OBJS} -lm -o $@

.c.o:
	${CC} ${FLAGS} $< -c -o $@

##########################
projeto_NOVO1.o:	projeto_NOVO1.c
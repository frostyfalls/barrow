include config.mk

SRC = barrow.c
OBJ = ${SRC:.c=.o}

barrow: ${OBJ}
	${CC} -o $@ ${LDFLAGS} ${OBJ}

.c.o:
	${CC} -c ${CFLAGS} $<

clean:
	rm -f barrow ${OBJ}

.PHONY: clean

CC=	cc
CFLAGS=	-O
RM=	rm -f
SHELL=	/bin/sh

all: sendwol

sendwol: sendwol.c
	$(CC) $(CFLAGS) -o sendwol sendwol.c

.PHONY: clean
clean:
	$(RM) sendwol

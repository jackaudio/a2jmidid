all: a2jmidid

CFLAGS+=-Wall -DSTANDALONE -Werror

ifeq ($(DEBUG),1)
CFLAGS+=-DDEBUG -g
else
CFLAGS+=-O2
endif

a2jmidid: a2jmidid.c
	$(CC) $(CFLAGS) $^ -o $@ -lrt -ljack -lasound

clean:
	rm -f *.o a2jmidid

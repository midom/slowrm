CFLAGS=`pkg-config glib-2.0 --cflags` -Wall -g
LDLIBS=`pkg-config glib-2.0 --libs`

all: slowrm

indent:
	indent -kr -ts4 -nut -l78 *.c

clean:
	rm -f -- *.o *~ slowrm

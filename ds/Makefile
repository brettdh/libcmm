LIBRARIES = libds.a

libds.a : ds_list.o ds_hash.o
	ar rv $@ $?
	ranlib $@

CFLAGS=-I. -Wall -Werror -g

clean:
	rm -f *.a *.o $(LIBRARIES) *~

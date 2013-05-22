
SHELL = /bin/bash
SOURCES = simple
FLAGS = -g -std=c99 -lHiDet -lksnap

all : $(SOURCES)

% : %.c
	gcc -std=c99 -o $@ $< $(FLAGS)

clean : 
	rm $(SOURCES);

tests:
	./simple;
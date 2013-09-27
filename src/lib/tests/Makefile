
SHELL = /bin/bash
SOURCES = simple
FLAGS = -g -std=gnu99 -lDetermTaskClock -lksnap -m32

all : $(SOURCES)

% : %.c
	gcc -o $@ $< $(FLAGS)

clean : 
	rm $(SOURCES);

tests:
	./simple;
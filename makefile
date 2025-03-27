all:
	gcc main.c -o csvis -DNCURSES_WIDECHAR=1 -lncursesw

d:
	gcc main.c -o csvis -DNCURSES_WIDECHAR=1 -lncursesw -ggdb3


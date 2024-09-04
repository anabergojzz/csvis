all:
	gcc csved.c -o csved -DNCURSES_WIDECHAR=1 -lncursesw

d:
	gcc csved.c -o csved -DNCURSES_WIDECHAR=1 -lncursesw -ggdb3


all:
	gcc csved.c -o csved -DNCURSES_WIDECHAR=1 -lncursesw

a:
	gcc a.c -o csved -DNCURSES_WIDECHAR=1 -lncursesw


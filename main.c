/* See LICENSE for license details. */

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <wchar.h>
#include <regex.h>

char *argv0;
#include "arg.h"

#define CELL_WIDTH 10
#define PIPE_BUF 4096
#define READALL_CHUNK 262144
#define SHELL "/bin/sh"
#define FIFO "/tmp/pyfifo"
#define XCLIP_COPY "xclip -selection clipboard -i"
#define XCLIP_PASTE "xclip -selection clipboard -o"
#define MOVE_X 3
#define MOVE_Y 5
#define FIRST "awk -F, -vOFS=, '"
#define LAST "'"

/* enums */
enum {
	PipeTo,
	PipeThrough,
	PipeRead,
	PipeAwk,
	PipeToClip,
	PipeReadClip,
	PipeReadInverse,
	WriteTo,
	WriteToInverse,
	WriteFifo,
	WriteExisting,
	PasteNormal,
	PasteInverse,
	Cut,
	Insert,
	Delete,
	Paste,
	DeleteCell,
	PasteCell,
	Undo,
	Redo
};

typedef union {
	int i;
} Arg;

typedef struct {
	int key[2];
	void (*func)(const Arg *arg);
	const Arg arg;
} Key;

struct undo {
	int operation;
	char ***mat;
	char *cell;
	int rows;
	int cols;
	int y;
	int x;
	int s_y;
	int s_x;
	int loc_y;
	int loc_x;
};

typedef struct node {
	struct undo *data;
	int dc;
	struct node *next;
	struct node *prev;
} node_t;
node_t *uhead = NULL;

struct Mat {
	char ***m;
	int rows;
	int cols;
	char *buff;
	size_t size;
};

void *xmalloc(size_t);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
void search(const Arg *);
void move_screen(const Arg *);
int statusbar(char *);
int readall(FILE *, char **, size_t *);
size_t utf8_strlen(const char *);
int wcswidth_total(const wchar_t *);
void format_wide_string(wchar_t *, size_t);
void draw(void);
void move_y_visual(void);
void move_x_visual(void);
void move_y(int);
void move_x(int);
void move_x_start(const Arg *);
void move_x_end(const Arg *);
void move_x_step(const Arg *);
void move_y_start(const Arg *);
void move_y_end(const Arg *);
void move_y_step(const Arg *);
void commands();
void when_resize(void);
void insert_row(const Arg *);
void insert_col(const Arg *);
void delete_row();
void delete_col();
char *get_str(char *, char, const char);
void visual_start();
void visual_end();
void visual();
void write_csv(const Arg *);
void write_to_cells(char *, int);
int pipe_through(char **, ssize_t *, char *);
void write_to_pipe(const Arg *);
void reg_init(void);
void yank_cells();
void wipe_cells();
void paste_cells(const Arg *);
void deleting();
void str_change(const Arg *);
void push(node_t **, struct undo *, int);
void undo(const Arg *);
void die(void);
void quit();
void keypress(int);
char ***write_to_matrix(char **, int *, int *);
void free_matrix(char ****, int, int);
void init_ui(void);
void usage(void);

/* globals */
struct Mat *matrice;
struct Mat *reg;
int rows, cols;
int y, x = 0;
int c_y, c_x = 0;
int v_y, v_x = 0;
int s_y, s_x = 0;
int s_y0, s_x0 = 0;
int y_0, x_0 = 0;
int ch[4] = {0, 0, 0, 0};
char mode = 'n';
int scr_x, scr_y;
char *fname = NULL;
int all_flag = 0;
int paste_flag = 0;
int delete_flag = 0;
char fs = ',';
char *srch = NULL;

static Key keys[] = {
	{{'q', -1}, quit, {0}},
	{{'v', -1}, visual_start, {0}},
	{{'V', -1}, visual, {0}},
	{{'\x03', -1}, visual_end, {0}}, /* Ctrl-C */
	{{'j', -1}, move_y_step, {.i = 1}},
	{{KEY_DOWN, -1}, move_y_step, {.i = 1}},
	{{'k', -1}, move_y_step, {.i = -1}},
	{{KEY_UP, -1}, move_y_step, {.i = -1}},
	{{'l', -1}, move_x_step, {.i = 1}},
	{{KEY_RIGHT, -1}, move_x_step, {.i = 1}},
	{{'h', -1}, move_x_step, {.i = -1}},
	{{KEY_LEFT, -1}, move_x_step, {.i = -1}},
	{{'\x04', -1}, move_y_step, {.i = MOVE_Y}}, /* Ctrl-D */
	{{'\x15', -1}, move_y_step, {.i = -MOVE_Y}}, /* Ctrl-U */
	{{'w', -1}, move_x_step, {.i = MOVE_X}},
	{{'b', -1}, move_x_step, {.i = -MOVE_X}},
	{{'G', -1}, move_y_end, {0}},
	{{'g', 'g'}, move_y_start, {0}},
	{{'$', -1}, move_x_end, {0}},
	{{'0', -1}, move_x_start, {0}},
	{{'c', -1}, str_change, {0}},
	{{'a', -1}, str_change, {2}},
	{{'i', -1}, str_change, {1}},
	{{'O', -1}, insert_row, {0}},
	{{'o', -1}, insert_row, {1}},
	{{'I', -1}, insert_col, {0}},
	{{'A', -1}, insert_col, {1}},
	{{'s', -1}, write_csv, {WriteTo}},
	{{'r', 's'}, write_csv, {WriteToInverse}},
	{{'e', -1}, write_csv, {WriteFifo}},
	{{'\x13', -1}, write_csv, {WriteExisting}}, /* Ctrl-S */
	{{'>', -1}, write_to_pipe, {PipeTo}},
	{{'|', -1}, write_to_pipe, {PipeThrough}},
	{{'\x0F', -1}, write_to_pipe, {PipeAwk}}, /* Ctrl-O awk */
	{{'<', -1}, write_to_pipe, {PipeRead}},
	{{'r', '<'}, write_to_pipe, {PipeReadInverse}},
	{{'d', -1}, wipe_cells, {0}},
	{{'y', -1}, yank_cells, {0}},
	{{'\x19', -1}, write_to_pipe, {PipeToClip}}, /* Ctrl-Y */
	{{'p', -1}, paste_cells, {PasteNormal}},
	{{'r', 'p'}, paste_cells, {PasteInverse}},
	{{'\x10', -1}, write_to_pipe, {PipeReadClip}}, /* Ctrl-P */
	{{'u', -1}, undo, {Undo}},
	{{'\x12', -1}, undo, {Redo}}, /* Ctrl-R */
	{{':', -1}, commands, {0}},
	{{'/', -1}, search, {0}},
	{{'n', -1}, search, {1}},
	{{'?', -1}, search, {2}},
	{{'N', -1}, search, {3}},
	{{'g', '/'}, search, {4}},
	{{'g', '?'}, search, {5}},
	{{'z', 't'}, move_screen, {0}},
	{{'z', 'b'}, move_screen, {1}},
	{{'z', 'z'}, move_screen, {2}},
	{{'D', -1}, deleting, {0}}
};

void *
xmalloc(size_t size)
	{
	void *ptr = malloc(size);
	if (ptr == NULL)
		{
		fprintf(stderr, "malloc: %s\n", strerror(errno));
		die();
		exit(EXIT_FAILURE);
		}
	return ptr;
	}

void *
xrealloc(void *ptr, size_t size)
	{
	void *new_ptr = realloc(ptr, size);
	if (new_ptr == NULL)
		{
		fprintf(stderr, "realloc: %s\n", strerror(errno));
		free(ptr);
		ptr = NULL;
		die();
		exit(EXIT_FAILURE);
		}
	return new_ptr;
	}

char *
xstrdup(const char *s)
	{
	char *p;
	if ((p = strdup(s)) == NULL)
		{
		fprintf(stderr, "strdup: %s\n", strerror(errno));
		die();
		exit(EXIT_FAILURE);
		}
	return p;
	}

void
search(const Arg *arg)
	{
	char *str;
	static int dir = 0;
	static int sel = 0;
	static int ch0, ch1, ch2, ch3;
	if (arg->i == 0 || arg->i == 2 || arg->i == 4 || arg->i == 5)
		{
		str = get_str("", 0, '/');
		if (str == NULL) return;
		else
			{
			if (srch) free(srch);
			srch = str;
			sel = 0;
			ch0 = 0;
			ch1 = matrice->rows;
			ch2 = 0;
			ch3 = matrice->cols;
			if (mode == 'v' && (arg->i == 4 || arg->i == 5))
				{
				sel = 1;
				ch0 = ch[0];
				ch1 = ch[1];
				ch2 = ch[2];
				ch3 = ch[3];
				}
			}
		if (arg->i == 0 || arg->i == 4) dir = 0;
		else if (arg->i == 2 || arg->i == 5) dir = 1;
		}
	else if (arg->i == 1 || arg->i == 3)
		{
		if (srch)
			str = srch;
		else
			return;
		}
	regex_t regex;
	int reti;
	char msgbuf[100];
	reti = regcomp(&regex, str, 0);
	if (reti)
		{
		statusbar("Could not compile regex");
		return;
		}

	int st_y;
	int st_x;
	char *temp;
	if (arg->i == 0 || arg->i == 4 || (arg->i == 1 && dir == 0) || (arg->i == 3 && dir == 1))
		{
		if (mode == 'v' && arg->i == 4)
			{
			st_y = ch0;
			st_x = ch2;
			if (ch1 - ch0 == 1 && ch3 - ch2 == 1)
				reti = REG_NOMATCH;
			}
		else
			{
			st_y = y;
			st_x = x;
			}
		for (int i = st_y; i < ch1; i++)
			{
			for (int j = ch2; j < ch3; j++)
				{
				if (i == st_y && j <= st_x) continue;
				temp = matrice->m[i][j];
				if (temp == NULL)
					temp = "";
				if (*str == '\0')
					{
					if (*temp == '\0')
						reti = 0;
					else
						reti = 1;
					}
				else
					reti = regexec(&regex, temp, 0, NULL, 0);
				if (!reti)
					{
					y = i;
					x = j;
					move_y_visual();
					move_x_visual();
					if (mode == 'v' && arg->i == 4)
						{
						ch[0] = ch[1] = ch[2] = ch[3] = 0;
						mode = 'n';
						}
					regfree(&regex);
					return;
					}
				}
			}
		}
	if (arg->i == 2 || arg->i == 5 || (arg->i == 3 && dir == 0) || (arg->i == 1 && dir == 1))
		{
		if (mode == 'v' && arg->i == 5)
			{
			st_y = ch1 - 1;
			st_x = ch3 - 1;
			if (ch1 - ch0 == 1 && ch3 - ch2 == 1)
				reti = REG_NOMATCH;
			}
		else
			{
			st_y = y;
			st_x = x;
			}
		for (int i = st_y; i >= ch0; i--)
			{
			for (int j = ch3-1; j >= ch2; j--)
				{
				if (i == st_y && j >= st_x) continue;
				temp = matrice->m[i][j];
				if (temp == NULL)
					temp = "";
				if (*str == '\0')
					{
					if (*temp == '\0')
						reti = 0;
					else
						reti = REG_NOMATCH;
					}
				else
					reti = regexec(&regex, temp, 0, NULL, 0);
				if (!reti)
					{
					y = i;
					x = j;
					move_y_visual();
					move_x_visual();
					if (mode == 'v' && arg->i == 5)
						{
						ch[0] = ch[1] = ch[2] = ch[3] = 0;
						mode = 'n';
						}
					regfree(&regex);
					return;
					}
				}
			}
		}
	if (reti == REG_NOMATCH || (st_y == matrice->rows - 1 && st_x == matrice->cols - 1))
		{
		if (sel == 1)
			statusbar("No further MATCH in last SELECTION.");
		else
			statusbar("No further MATCH.");
		}
	else
		{
		regerror(reti, &regex, msgbuf, sizeof(msgbuf));
		statusbar(msgbuf);
		}
	if (mode == 'n')
		ch[0] = ch[1] = ch[2] = ch[3] = 0;
	regfree(&regex);
	return;
	}

void
move_screen(const Arg *arg)
	{
	if (arg->i == 0)
		s_y = y;
	else if (arg->i == 1)
		{
		if (y >= scr_y - 1)
			s_y = y - scr_y;
		}
	else if (arg->i == 2)
		{
		if (y >= scr_y/2 - 1)
			s_y = y - scr_y/2;
		}
	}

int
statusbar(char *string)
	{
	mvprintw(rows - 1, 0, " ");
	wclrtoeol(stdscr);
	mvprintw(rows - 1, 1, "%s", string);
	return getch();
	}

int
readall(FILE *in, char **dataptr, size_t *sizeptr)
	{
	char *data = NULL, *temp;
	size_t size = 0;
	size_t used = 0;
	size_t n;

	/* If empty or no file */
	if (in == NULL)
		{
		*dataptr = xstrdup("\n");
		*sizeptr = 1;
		return 0;
		}

	/* None of the parameters can be NULL */
	if (dataptr == NULL || sizeptr == NULL)
		exit(EXIT_FAILURE);

	/* A read error already occurred? */
	if (ferror(in))
		exit(EXIT_FAILURE);

	while (1)
		{
		if (used + READALL_CHUNK + 1 > size)
			{
			size = used + READALL_CHUNK + 1;
			if (size <= used)
				{ free(data); exit(EXIT_FAILURE); }
			temp = realloc(data, size);
			if (temp == NULL)
				{ free(data); exit(EXIT_FAILURE); }
			data = temp;
			}
		
		n = fread(data + used, 1, READALL_CHUNK, in);
		if (n == 0)
			break;
		used += n;
		}
	
	if (ferror(in))
		{ free(data); exit(EXIT_FAILURE); }

	temp = realloc(data, used + 1);
	if (temp == NULL)
		{ free(data); exit(EXIT_FAILURE); }
	data = temp;
	data[used] = '\0';

	*dataptr = data;
	*sizeptr = used;
	return 0;
	}

size_t
utf8_strlen(const char *str)
	{
	mbstate_t state = {0};
	const char *s = str;
	size_t len = 0;
	while (*s)
		{
		size_t ret = mbrlen(s, MB_CUR_MAX, &state);
		if (ret == (size_t)-1 || ret == (size_t)-2)
			break;
		s += ret;
		len++;
		}
	return len;
	}

int
wcswidth_total(const wchar_t *wstr)
	{
	int total_width = 0;
	for (size_t i = 0; wstr[i] != L'\0'; i++)
		{
		int width = wcwidth(wstr[i]);
		if (width == -1)
			return -1; /* invalid character */
		total_width += width;
		}
	return total_width;
	}

void
format_wide_string(wchar_t *buffer, size_t max_width)
	{
	size_t len = 0;
	size_t i = 0;

	while (buffer[i] != L'\0' && len < max_width)
		{
		int char_width = wcwidth(buffer[i]);
		if (char_width < 0) char_width = 0;
		if (len + char_width > max_width)
			break;
		len += char_width;
		i++;
		}

	while (len < max_width)
		{
		len++;
		buffer[i++] = L' ';
		}
	buffer[i] = L'\0';
	}

void
draw(void)
	{
	werase(stdscr);
	for (int i = 0; i < scr_y; i++)
		{
		for (int j = 0; j < scr_x; j++)
			{
			if (ch[0] <= s_y + i && s_y + i < ch[1] && ch[2] <= s_x + j && s_x + j < ch[3])
				attron(A_STANDOUT);
			else attroff(A_STANDOUT);
			char *cell_value = matrice->m[i + s_y][j + s_x];
			if (cell_value == NULL) cell_value = "";
			wchar_t buffer[CELL_WIDTH];
			mbstowcs(buffer, cell_value, CELL_WIDTH - 1);
			buffer[CELL_WIDTH - 1] = L'\0';
			format_wide_string(buffer, CELL_WIDTH - 1);
			mvaddwstr(i, j * CELL_WIDTH, buffer);
			}
		}
	wmove(stdscr, c_y, c_x);
	attroff(A_STANDOUT);
	}

void
move_y_visual(void)
	{
	if (mode == 'v')
		{
		if (y >= v_y)
			{ ch[0] = v_y; ch[1] = y + 1; }
		else
			{ ch[0] = y; ch[1] = v_y + 1; }
		}
	}

void
move_x_visual(void)
	{
	if (mode == 'v')
		{
		if (x >= v_x)
			{ ch[2] = v_x; ch[3] = x + 1; }
		else
			{ ch[2] = x; ch[3] = v_x + 1; }
		}
	}

void
move_y(int move)
	{
	y += move;
	if (y >= matrice->rows)
		y = matrice->rows - 1;
	else if (y < 0)
		y = 0;
	move_y_visual();
	}

void
move_x(int move)
	{
	x += move;
	if (x >= matrice->cols)
		x = matrice->cols - 1;
	else if (x < 0)
		x = 0;
	move_x_visual();
	}

void
move_x_start(const Arg *arg)
	{
	x = 0;
	move_x_visual();
	}

void
move_x_end(const Arg *arg)
	{
	x = matrice->cols - 1;
	move_x_visual();
	}

void
move_x_step(const Arg *arg)
	{
	move_x(arg->i);
	}

void
move_y_start(const Arg *arg)
	{
	y = 0;
	move_y_visual();
	}

void
move_y_end(const Arg *arg)
	{
	y = matrice->rows - 1;
	move_y_visual();
	}

void
move_y_step(const Arg *arg)
	{
	move_y(arg->i);
	}

void
commands()
	{
	char *temp = get_str("", 0, ':');
	if (temp == NULL) return;
	else if (temp[0] == 'f')
		{
			fs = temp[2];
			statusbar("Field separator set!");
			free(temp);
			return;
		}
	int length = strlen(temp);
	int to_num_y = 0;
	int to_num_x = x;
	int is_number = 0;
	char next = 0;

	for (int i = 0; i < length; i++)
		{
		if (*(temp + i) >= '0' && *(temp + i) <= '9')
			{
			if (next == 0)
				to_num_y = to_num_y * 10 + (*(temp + i) - '0');
			else if (next == 1)
				to_num_x = to_num_x * 10 + (*(temp + i) - '0');
			is_number = 1;
			}
		else if (*(temp + i) == '.')
			{
			if (is_number == 0)
				to_num_y = y;
			next = 1;
			to_num_x = 0;
			is_number = 0;
			}
		else
			break;
		}
	free(temp);
	if (is_number)
		{
		to_num_y -= y;
		to_num_x -= x;
		Arg move;
		if (to_num_y != 0 && y + to_num_y < matrice->rows && y + to_num_y >= 0)
			{
			move.i = to_num_y;
			move_y_step(&move);
			}
		if (to_num_x != 0 && x + to_num_x < matrice->cols && x + to_num_x >= 0)
			{
			move.i = to_num_x;
			move_x_step(&move);
			}
		}
	}

void
when_resize(void)
	{
	getmaxyx(stdscr, rows, cols);
	scr_x = cols/CELL_WIDTH;
	if (scr_x > matrice->cols) scr_x = matrice->cols;
	scr_y = rows;
	if (scr_y > matrice->rows) scr_y = matrice->rows;
	if (y <= s_y) /* if y above screen */
		s_y = y;
	else if (y >= s_y + scr_y) /* if y below screen */
		s_y = y - (scr_y - 1);
	if (scr_y - (matrice->rows - s_y) > 0) /* correct s_y when increasing window size to expand to whole window size */
		s_y -= scr_y - (matrice->rows - s_y);
	if (x <= s_x) /* if x left of screen */
		s_x = x;
	else if (x >= s_x + scr_x) /* if x right of screen */
		s_x = x - (scr_x - 1);
	if (scr_x - (matrice->cols - s_x) > 0) /* correct s_x when resizing */
		s_x -= scr_x - (matrice->cols - s_x);
	c_x = (x - s_x)*CELL_WIDTH;
	c_y = y - s_y;
	}

void insert_row(const Arg *arg)
	{
	y += arg->i;
	matrice->m = xrealloc(matrice->m, (matrice->rows + 1) * sizeof(char *));
	for (int i = matrice->rows; i > y; i--)
		matrice->m[i] = matrice->m[i - 1];
	matrice->m[y] = xmalloc(matrice->cols * sizeof(char *));
	for (int j = 0; j < matrice->cols; j++)
		matrice->m[y][j] = NULL;
	matrice->rows++;
	struct undo data[] = {{Insert, NULL, NULL, 1, 0, y, x, s_y, s_x, y, x}};
	push(&uhead, data, 1);
	}

void
insert_col(const Arg *arg)
	{
	x += arg->i;
	for (int i = 0; i < matrice->rows; i++)
		{
		matrice->m[i] = xrealloc(matrice->m[i], (matrice->cols + 1) * sizeof(char *));
		for (int j = matrice->cols; j > x; j--)
			matrice->m[i][j] = matrice->m[i][j - 1];
		matrice->m[i][x] = NULL;
		}
	matrice->cols++;
	struct undo data[] = {{Insert, NULL, NULL, 0, 1, y, x, s_y, s_x, y, x}};
	push(&uhead, data, 1);
	}

void
delete_row()
	{
	reg_init();
	char ***undo_mat = xmalloc(reg->rows * sizeof(char **));
	char *current_ptr = reg->buff;
	for (int i = ch[0]; i < ch[1]; i++)
		{
		undo_mat[i - ch[0]] = matrice->m[i];
		for (int j = ch[2]; j < ch[3]; j++)
			{
			if (matrice->m[i][j] == NULL) matrice->m[i][j] = "";
			strcpy(current_ptr, matrice->m[i][j]);
			reg->m[i - ch[0]][j - ch[2]] = current_ptr;
			current_ptr += strlen(matrice->m[i][j]) + 1;
			}
		}
	for (int i = ch[0]; i < matrice->rows - reg->rows; i++)
		matrice->m[i] = matrice->m[i + reg->rows];
	matrice->rows -= reg->rows;
	struct undo data[] = {
		{Delete, undo_mat, NULL, reg->rows, reg->cols, y_0, x_0, s_y, s_x, ch[0], ch[2]},
		{Cut, NULL, NULL, reg->rows, 0, ch[0], x, s_y, s_x, ch[0], x},
		{}
	};
	 if (matrice->rows == 0)
	 	{
	 	data[1].rows--;
	 	data[2] = (struct undo){Cut, NULL, NULL, 0, reg->cols-1, 0, 0, 0, 0, 0, 0};
	 	matrice->rows = matrice->cols = 1;
	 	x = 0;
	 	matrice->m[0] = xmalloc(sizeof(char *));
		matrice->m[0][0] = NULL;
	 	push(&uhead, data, 3);
	 	}
	 else
		push(&uhead, data, 2);
	matrice->m = xrealloc(matrice->m, matrice->rows * sizeof(char **));
	y = ch[0];
	if (y >= matrice->rows)
		y = ch[0] - 1;
	ch[0] = ch[1] = ch[2] = ch[3] = 0;
	mode = 'n';
	paste_flag = 3;
	}

void
delete_col()
	{
	reg_init();
	char ***undo_mat = xmalloc(reg->rows * sizeof(char**));
	for (int i = 0; i < reg->rows; i++)
		undo_mat[i] = xmalloc(reg->cols * sizeof(char*));
	char *current_ptr = reg->buff;
	for (int i = ch[0]; i < ch[1]; i++)
		{
		for (int j = ch[2]; j < ch[3]; j++)
			{
			undo_mat[i - ch[0]][j - ch[2]] = matrice->m[i][j];
			if (matrice->m[i][j] == NULL) matrice->m[i][j] = "";
			strcpy(current_ptr, matrice->m[i][j]);
			reg->m[i - ch[0]][j - ch[2]] = current_ptr;
			current_ptr += strlen(matrice->m[i][j]) + 1;
			matrice->m[i][j] = NULL;
			}
		for (int j = ch[2]; j < matrice->cols - reg->cols; j++)
			matrice->m[i][j] = matrice->m[i][j + reg->cols];
		}
	matrice->cols -= reg->cols;
	struct undo data[] = {
		{Delete, undo_mat, NULL, reg->rows, reg->cols, y_0, x_0, s_y, s_x, ch[0], ch[2]},
		{Cut, NULL, NULL, 0, reg->cols, y, ch[2], s_y, s_x, y, ch[2]}
	};
	push(&uhead, data, 2);
	x = ch[2];
	if (x >= matrice->cols)
		x = ch[2] - 1;
	ch[0] = ch[1] = ch[2] = ch[3] = 0;
	mode = 'n';
	paste_flag = 4;
	}

char *
get_str(char *str, char loc, const char cmd)
	{
	if (str == NULL) str = "";
	size_t str_size = mbstowcs(NULL, str, 0);
	size_t bufsize = str_size + 32; /* Initial buffer size */
	wchar_t* buffer = xmalloc(bufsize * sizeof(wchar_t));
	mbstowcs(buffer, str, str_size + 1);
	size_t i = 0; /* Position in buffer */
	if (loc == 1) i = str_size;
	int cx_add, cy_add;
	size_t c_xtemp;

	int hidden_text = 0;
	size_t line_widths_size = 8;
	int *line_widths = xmalloc(line_widths_size * sizeof(int));
	s_y0 = s_y;

	while (1)
		{
		when_resize();
		if (cmd != 0)
			{ c_x = 0; c_y = rows - 1; }
		int l = 0; /* number of characters in each line */
		c_xtemp = c_x;
		cx_add = 0, cy_add = 0;
		for (size_t j = 0; j < i; j++)
			{
			int width = wcwidth(buffer[j]);
			if (width == -1)
				{
				statusbar("Invalid character encountered.");
				free(buffer);
				free(line_widths);
				return NULL;
				}
			if (cx_add + width == cols - c_xtemp)
				{
				line_widths[cy_add] = l + 1;
				l = 0;
				cy_add++;
				cx_add = 0;
				c_xtemp = 0;
				}
			else if (cx_add + width >= cols - c_xtemp)
				{
				line_widths[cy_add] = l;
				l = 1;
				cy_add++;
				cx_add = width;
				c_xtemp = 0;
				}
			else
				{ cx_add += width; l++; }
			if (cy_add >= line_widths_size)
				{
				line_widths_size *= 2;
				line_widths = xrealloc(line_widths, line_widths_size*sizeof(int));
				}
			}
		int s = c_y + cy_add - rows + 1;
		if (s > 0) /* if bottom of screen reached */
			{
			if (cmd == 0)
				{
				s_y += s;
				if (matrice->rows - s_y < rows) scr_y -= s; /* if last row on screen */
				}
			c_y -= s;
			if (c_y < 0) /* if insert start position above window */
				{
				hidden_text = 0;
				for (int i = 0; i < -c_y; i++)
					hidden_text += line_widths[i];
				cy_add += c_y;
				scr_y = 0;
				c_y = 0;
				c_x = 0;
				}
			else hidden_text = 0;
			}
		else hidden_text = 0;

		draw();
		mvprintw(c_y, c_x, "%*s", CELL_WIDTH, "");
		if (cmd != 0)
			{ mvaddch(c_y, c_x, cmd); c_x++; c_xtemp++; }
		mvaddwstr(c_y, c_x, buffer + hidden_text);
		int c_yend, c_xend;
		getyx(stdscr, c_yend, c_xend);
		if (!(c_yend == rows-1 && c_xend == cols-1))
			addch(' ');

		wmove(stdscr, c_y + cy_add, c_xtemp + cx_add);

		if (str_size + 1 >= bufsize)
			{
			bufsize *= 2;
			buffer = xrealloc(buffer, bufsize * sizeof(wchar_t));
			}

		wint_t key;
		int ret = get_wch(&key);
		if (ret == OK)
			{
			if (key == '\n')
				{
				if (cmd == 0)
					mode = 'i';
				break;
				}
			else if (key == '\t')
				{
				if (cmd == 0)
					mode = 'j';
				break;
				}
			else if (key == 127)
				{
				if (i > 0)
					{
					i--;
					wmemmove(buffer + i, buffer + i + 1, str_size-- - i);
					}
				}
			else if (key == '\x03')
				{
				if (cmd == 0)
					{
					mode = 'n';
					break;
					}
				else
					{
					free(buffer);
					free(line_widths);
					return NULL;
					}
				}
			else if (key >= 32)
				{
				wmemmove(buffer + i + 1, buffer + i, str_size - i + 1);
				buffer[i++] = (wchar_t)key;
				str_size++;
				}
			}
		else if (ret == KEY_CODE_YES)
			{
			if (key == KEY_LEFT)
				{ if (i > 0) i--; }
			else if (key == KEY_RIGHT)
				{ if (i < str_size) i++; }
			else if (key == KEY_HOME)
				i = 0;
			else if (key == KEY_END)
				i = str_size;
			else if (key == KEY_BACKSPACE)
				{
				if (i > 0)
					{
					i--;
					wmemmove(buffer + i, buffer + i + 1, str_size-- - i);
					}
				}
			else if (key == KEY_DC)
				{
				if (i < str_size)
					wmemmove(buffer + i, buffer + i + 1, str_size-- - i);
				}
			}
		}

	size_t mb_len = wcstombs(NULL, buffer, 0) + 1;
	char* rbuffer = xmalloc(mb_len);
	wcstombs(rbuffer, buffer, mb_len);
	free(buffer);
	free(line_widths);
	s_y = s_y0; /* revert to screen position before insertion */

	return rbuffer;
	}

void
visual_start()
	{
	if (mode != 'v')
		{
		mode = 'v';
		s_y0 = s_y;
		s_x0 = s_x;
		v_y = y;
		v_x = x;
		y_0 = y;
		x_0 = x;
		ch[0] = y;
		ch[1] = y + 1;
		ch[2] = x;
		ch[3] = x + 1;
		}
	else
		{
		mode = 'n';
		ch[0] = ch[1] = ch[2] = ch[3] = 0;
		}
	all_flag = 0;
	}
 
void
visual_end()
	{
	ch[0] = ch[1] = ch[2] = ch[3] = 0;
	if (mode != 'n')
		{
		mode = 'n';
		y = y_0;
		x = x_0;
		s_y = s_y0;
		s_x = s_x0;
		}
	all_flag = 0;
	}

void
visual()
	{
	Arg move;
	move.i = 0;
	int key;
	key = getch();
	if (key == 'l' || key == 'h' || key == '$' || key == '0' ||
			key == 'w' || key == 'b' || key == KEY_RIGHT || key == KEY_LEFT)
		{
		mode = 'n';
		visual_start();
		all_flag = 2;
		v_y = 0;
		ch[0] = 0;
		ch[1] = matrice->rows;
		ch[2] = x;
		ch[3] = x + 1;
		if (key == '$')
			{ move_x_end(&move); }
		if (key == '0')
			{ move_x_start(&move); }
		if (key == 'w')
			{ move.i = MOVE_X; move_x_step(&move); }
		if (key == 'b')
			{ move.i = -MOVE_X; move_x_step(&move); }
		}
	else if (key == 'j' || key == 'k' || key == 'G' || key == 'g' ||
			key == '\x04' || key == '\x15' || key == KEY_UP || key == KEY_DOWN)
		{
		mode = 'n';
		visual_start();
		all_flag = 1;
		v_x = 0;
		ch[0] = y;
		ch[1] = y + 1;
		ch[2] = 0;
		ch[3] = matrice->cols;
		if (key == 'G')
			{ move_y_end(&move); }
		if (key == 'g')
			{ move_y_start(&move); }
		if (key == '\x04')	
			{ move.i = MOVE_Y; move_y_step(&move); }
		if (key == '\x15')
			{ move.i = -MOVE_Y; move_y_step(&move); }
		}
	}

void
write_csv(const Arg *arg)
	{
	char *filename = NULL;

	if (arg->i == WriteTo || arg->i == WriteToInverse)
		{
		filename = get_str("", 0, ':');
		if (filename == NULL) return;
		if (filename != fname)
			{
			FILE *test_existing = fopen(filename, "r");
			if (test_existing != NULL)
				{
				fclose(test_existing);
				int status = statusbar("File already exists! Overwrite? [y][n]");
				if (status != 'y')
					{
					free(filename);
					return;
					}
				}
			if (fname == NULL)
				{
				fname = xstrdup(filename);
				printf("\e]0;%s - csvis\a", fname);
				fflush(stdout);
				}
			}
		}
	else if (arg->i == WriteExisting)
		{
		if (fname == NULL)
			{
			filename = get_str("", 0, ':');
			if (filename == NULL) return;
			else
				{
				FILE *test_existing = fopen(filename, "r");
				if (test_existing != NULL)
					{
					fclose(test_existing);
					int status = statusbar("File already exists! Overwrite? [y][n]");
					if (status != 'y')
						{
						free(filename);
						return;
						}
					}
				fname = xstrdup(filename);
				printf("\e]0;%s - csvis\a", fname);
				fflush(stdout);
				}
			}
		else
			{
			filename = xmalloc(strlen(fname) + 1);
			strcpy(filename, fname);
			}
		visual_end();
		}
	else
		{
		filename = xmalloc(strlen(FIFO) + 1);
		strcpy(filename, FIFO);
		int fd = open(filename, O_WRONLY | O_NONBLOCK);
		if (fd == -1)
			{
			if (errno == ENXIO)
				statusbar("Nobody listens.");
			else
				statusbar("Error opening named pipe.");
			close(fd);
			free(filename);
			return;
			}
		close(fd);
		}
	if (strlen(filename) == 0)
		statusbar("Empty filename.");
	else
		{
		FILE *file = fopen(filename, "w");
		if (!file)
			statusbar("Error opening file for writing");

		if (mode == 'n')
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = 0;
			ch[3] = matrice->cols;
			}
		if (arg->i == WriteToInverse) 
			{
			int temp1, temp2;
			temp1 = ch[0];
			temp2 = ch[1];
			ch[0] = ch[2];
			ch[1] = ch[3];
			ch[2] = temp1;
			ch[3] = temp2;
			}
		char *first = "";
		char *end = "";
		if (arg->i == WriteFifo)
			{ first = "=["; end = "]"; }
		for (int i = ch[0]; i < ch[1]; i++)
			{
			for (int j = ch[2]; j < ch[3]; j++)
				{
				char *inverse = NULL;
				if (WriteToInverse == 1)
					inverse = matrice->m[j][i];
				else
					inverse = matrice->m[i][j];
				if (inverse != NULL) fprintf(file, "%s", inverse);
				if (j == ch[3]-1)
					{
					if (*end != '\0' && j != ch[2])
						fprintf(file, "%s", end);
					fprintf(file, "\n");
					}
				else if (j == ch[2] && *first != '\0')
					fprintf(file, "%s", first);
				else
					fprintf(file, "%c", fs);
				}
			}
		fclose(file);
		}

	free(filename);
	visual_end();
	if (arg->i == WriteTo || arg->i == WriteExisting || arg->i == WriteToInverse)
		statusbar("Saved!");
	}

void
write_to_cells(char *buffer, int arg)
	{
	int cols, rows;
	char *inverse = NULL;;
	char ***temp = write_to_matrix(&buffer, &rows, &cols);
	if (temp == NULL) return;
	if (arg == PipeReadInverse)
		{
		int temp_rows = rows;
		rows = cols;
		cols = temp_rows;
		}
	char ***undo_mat0;
	if (mode == 'v')
		{
		undo_mat0 = xmalloc((ch[1] - ch[0]) * sizeof(char **));
		for (int i = 0; i < (ch[1] - ch[0]); i++)
			{
			undo_mat0[i] = xmalloc((ch[3] - ch[2]) * sizeof(char *));
			for (int j = 0; j < (ch[3] - ch[2]); j++)
				{
				undo_mat0[i][j] = matrice->m[ch[0] + i][ch[2] + j];
				matrice->m[ch[0] + i][ch[2] + j] = NULL;
				}
			}
		}
	else
		undo_mat0 = NULL;
	char ***paste_mat = xmalloc(rows * sizeof(char **));
	char ***undo_mat = xmalloc(rows * sizeof(char **));
	int add_y, add_x = 0;
	if ((add_y = ch[0] + rows - matrice->rows) < 0) add_y = 0;
	if (add_y > 0) /* If not enough rows */
		{
		matrice->m = xrealloc(matrice->m, (matrice->rows + add_y) * sizeof(char **));
		for (int i = matrice->rows; i < matrice->rows + add_y; i++)
			{
			matrice->m[i] = xmalloc(matrice->cols * sizeof(char *));
			for (int j = 0; j < matrice->cols; j++)
				matrice->m[i][j] = NULL;
			}
		matrice->rows += add_y;
		}
	if ((add_x = ch[2] + cols - matrice->cols) < 0) add_x = 0;
	if (add_x > 0) /* If not enough cols */
		{
		for (int i = 0; i < matrice->rows; i++)
			{
			matrice->m[i] = xrealloc(matrice->m[i], (matrice->cols + add_x) * sizeof(char *));
			for (int j = matrice->cols; j < matrice->cols + add_x; j++)
				matrice->m[i][j] = NULL;
			}
		matrice->cols += add_x;
		}
	for (int i = 0; i < rows; i++)
		{
		undo_mat[i] = xmalloc(cols * sizeof(char *));
		paste_mat[i] = xmalloc(cols * sizeof(char *));
		for (int j = 0; j < cols; j++)
			{
			if (arg == PipeReadInverse) inverse = temp[j][i];
			else inverse = temp[i][j];
			if (inverse != NULL)
				{
				undo_mat[i][j] = matrice->m[ch[0] + i][ch[2] + j];
				paste_mat[i][j] = inverse;
				matrice->m[ch[0] + i][ch[2] + j] = inverse;
				}
			else
				{
				undo_mat[i][j] = NULL;
				paste_mat[i][j] = NULL;
				}
			}
		}
	struct undo data[] = {
		{Delete, undo_mat0, NULL, ch[1] - ch[0], ch[3] - ch[2], ch[0], ch[2], s_y, s_x, ch[0], ch[2]},
		{Delete, undo_mat, NULL, rows, cols, ch[0], ch[2], s_y, s_x, ch[0], ch[2]},
		{Insert, NULL, NULL, add_y, add_x, ch[0], ch[2], s_y, s_x, matrice->rows-add_y, matrice->cols-add_x},
		{Paste, paste_mat, buffer, rows, cols, ch[0], ch[2], s_y, s_x, ch[0], ch[2]}
	};
	push(&uhead, data, 4);
	if (arg == PipeReadInverse)
		{
		int temp_rows = rows;
		rows = cols;
		cols = temp_rows;
		}
	for (int i = 0; i < rows; i++) free(temp[i]);
	free(temp);
	}

int
pipe_through(char **output_buffer, ssize_t *output_buffer_size, char *cmd)
	{
	int pin[2], pout[2], perr[2], status = -1;
	
	if (pipe(pin) == -1)
		return -1;
	if (pipe(pout) == -1)
		{
		close(pin[0]);
		close(pin[1]);
		return -1;
		}
	if (pipe(perr) == -1)
		{
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		return -1;
		}

	signal(SIGPIPE, SIG_IGN);
	pid_t pid = fork();

	if (pid == -1)
		{
		close(pin[0]);
		close(pin[1]);
		close(pout[0]);
		close(pout[1]);
		close(perr[0]);
		close(perr[1]);
		statusbar("Failed to fork.");
		return -1;
		}
	else if (pid == 0)
		{
		close(pin[1]);
		close(pout[0]);
		close(perr[0]);
		dup2(pin[0], STDIN_FILENO);
		dup2(pout[1], STDOUT_FILENO);
		dup2(perr[1], STDERR_FILENO);
		close(pin[0]);
		close(pout[1]);
		close(perr[1]);

		execlp(SHELL, SHELL, "-c", cmd, (char *)NULL);
		/* if execlp witout success */
		exit(EXIT_FAILURE);
		}

	close(pin[0]);
	close(pout[1]);
	close(perr[1]);

	fcntl(pout[0], F_SETFL, O_NONBLOCK);
	fcntl(perr[0], F_SETFL, O_NONBLOCK);

	char buffer[PIPE_BUF];
	ssize_t nread, nwritten;
	size_t buffer_len = 0;
	int row = ch[0], col = ch[2];
	size_t pos = 0, pos_str = 0;
	ssize_t buffer_capacity = 0;
	char buferror[4096];
	size_t poserror = 0;
	int attempts = 0;
	const int max_attempts = 4000;

	while (pin[1] != -1 || pout[0] != -1 || perr[0] != -1)
		{
		if (pin[1] != -1)
			{
			if (buffer_len == 0 && row < ch[1])
				{
				pos = 0;
				for (; row < ch[1]; row++)
					{
					for (; col < ch[3]; col++)
						{
						char *temp = matrice->m[row][col];
						if (temp == NULL) temp = "";
						size_t len = strlen(temp) - pos_str;
						if (pos + len >= PIPE_BUF)
							{
							memcpy(buffer + pos, temp + pos_str, PIPE_BUF - pos);
							pos_str += PIPE_BUF - pos;
							pos = PIPE_BUF;
							break;
							}
						memcpy(buffer + pos, temp + pos_str, len);
						pos += len;
						pos_str = 0;
						if (col < ch[3] - 1)
							buffer[pos++] = fs;
						else
							buffer[pos++] = '\n';
						}
					if (col < ch[3]) break;
					col = ch[2];
					}
				buffer_len = pos;
				}
			if (buffer_len > 0)
				{
				nwritten = write(pin[1], buffer, buffer_len);
				if (nwritten > 0)
					{
					buffer_len -= nwritten;
					memmove(buffer, buffer + nwritten, buffer_len);
					}
				else
					{
					close(pin[1]);
					pin[1] = -1;
					}
				}
			if (row >= ch[1] && buffer_len == 0)
				{
				close(pin[1]);
				pin[1] = -1;
				}
			}

		if (pout[0] != -1)
			{
			char buf[PIPE_BUF];
			nread = read(pout[0], buf, sizeof(buf));
			if (*output_buffer_size + nread + 1 > buffer_capacity)
				{
				buffer_capacity = buffer_capacity ? buffer_capacity * 2 : (nread * 2) + 1;
				char *newp = realloc(*output_buffer, buffer_capacity);
				if (newp == NULL)
					{
					statusbar("Cannot reallocate memory.");
					close(pout[0]);
					close(pin[1]);
					close(perr[0]);
					return -1;
					}
				*output_buffer = newp;
				}
			if (nread > 0)
				{
				memcpy(*output_buffer + *output_buffer_size, buf, nread);
				*output_buffer_size += nread;
				}
			else if (nread == 0)
				{
				*(*output_buffer + *output_buffer_size) = '\0';
				(*output_buffer_size)++;
				close(pout[0]);
				pout[0] = -1;
				}
			else if (errno == EAGAIN)
				{
				if (waitpid(pid, NULL, WNOHANG) == pid)
					{
					close(pout[0]);
					pout[0] = -1;
					}
				}
			else if (errno != EINTR && errno != EWOULDBLOCK)
				{
				statusbar("Error reading from stdout.");
				close(pout[0]);
				pout[0] = -1;
				break;
				}
			}

		if (perr[0] != -1)
			{
			char buf[PIPE_BUF];
			nread = read(perr[0], buf, sizeof(buf));
			if (nread > 0)
				{
				memcpy(buferror + poserror, buf, nread);
				poserror += nread;
				}
			else if (nread == 0)
				{
				close(perr[0]);
				perr[0] = -1;
				}
			else if (errno != EINTR && errno != EWOULDBLOCK)
				{
				statusbar("Error reading from stderr.");
				close(perr[0]);
				perr[0] = -1;
				break;
				}
			else if (errno == EAGAIN)
				{
				if (++attempts > max_attempts)
					{
					close(perr[0]);
					perr[0] = -1;
					}
				}
			}
		}

	waitpid(pid, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		{
		statusbar(buferror);
		return -1;
		}
	return 0;
	}

void
write_to_pipe(const Arg *arg)
	{
	char *cmd;
	if (arg->i == PipeThrough)
		{
		cmd = get_str("", 0, '|');
		if (cmd == NULL) return;
		}
	else if (arg->i == PipeTo)
		{
		cmd = get_str("", 0, '>');
		if (cmd == NULL) return;
		}
	else if (arg->i == PipeRead || arg->i == PipeReadInverse)
		{
		cmd = get_str("", 0, '<');
		if (cmd == NULL) return;
		}
	else if (arg->i == PipeAwk)
		{
		char *temp = get_str("", 0, '|');
		if (temp == NULL) return;
		char *preposition = FIRST;
		cmd = xmalloc(strlen(temp) + strlen(preposition) + 2);
		strcpy(cmd, preposition);
		strcpy(cmd + strlen(preposition), temp);
		strcpy(cmd + strlen(preposition) + strlen(temp), LAST);
		free(temp);
		}
	else if (arg->i == PipeToClip)
		{
		cmd = xmalloc(30);
		strcpy(cmd, XCLIP_COPY);
		}
	else if (arg->i == PipeReadClip)
		{
		cmd = xmalloc(30);
		strcpy(cmd, XCLIP_PASTE);
		}
	if (strlen(cmd) == 0)
		{
		free(cmd);
		return;
		}

	if (arg->i != PipeRead && arg->i != PipeReadInverse && arg->i != PipeReadClip && mode == 'n')
		{
		ch[0] = 0;
		ch[1] = matrice->rows;
		ch[2] = 0;
		ch[3] = matrice->cols;
		}
	else if (arg->i == PipeRead || arg->i == PipeReadInverse || arg->i == PipeReadClip)
		{
		ch[0] = y;
		ch[2] = x;
		}

	char *output_buffer = NULL;
	ssize_t output_buffer_size = 0;
	if (pipe_through(&output_buffer, &output_buffer_size, cmd) == -1)
		{
		if (mode == 'n') visual_end();
		return;
		}
	free(cmd);

	if (arg->i == PipeToClip);
	else if (output_buffer_size > 0)
		{
		if (arg->i == PipeTo)
			{
			werase(stdscr);
			mvprintw(0, 0, "%s", output_buffer);
			getch();
			}
		else
			write_to_cells(output_buffer, arg->i);
		}
	visual_end();
	}

void
reg_init(void)
	{
	if (reg == NULL)
		{
		reg = xmalloc(sizeof(struct Mat));
		reg->m = NULL;
		reg->buff = NULL;
		}
	if (reg->m)
		{
		free_matrix(&reg->m, reg->rows, reg->cols);
		free(reg->buff);
		reg->m = NULL;
		reg->buff = NULL;
		}
	reg->rows = ch[1] - ch[0];
	reg->cols = ch[3] - ch[2];
	reg->size = 0;
	for (int i = ch[0]; i < ch[1]; i++)
		{
		for (int j = ch[2]; j < ch[3]; j++)
			{
			if (matrice->m[i][j] != NULL)
				reg->size += strlen(matrice->m[i][j]) + 1;
			else
				reg->size += 1;
			}
		}
	reg->buff = xmalloc(reg->size * sizeof(char));
	reg->m = xmalloc(reg->rows * sizeof(char **));
	for (int i = 0; i < reg->rows; i++)
		reg->m[i] = xmalloc(reg->cols * sizeof(char *));
	}

void
yank_cells()
	{
	reg_init();
	char *current_ptr = reg->buff;
	for (int i = ch[0]; i < ch[1]; i++)
		{
		for (int j = ch[2]; j < ch[3]; j++)
			{
			char *temp = matrice->m[i][j];
			if (temp != NULL)
				{
				strcpy(current_ptr, matrice->m[i][j]);
				reg->m[i - ch[0]][j - ch[2]] = current_ptr;
				current_ptr += strlen(matrice->m[i][j]) + 1;
				}
			else reg->m[i - ch[0]][j - ch[2]] = NULL;
			}
		}
	if (all_flag == 1) paste_flag = 1;
	else if (all_flag == 2) paste_flag = 2;
	else paste_flag = 0;
	visual_end();
	}

void
wipe_cells()
	{
	if (mode == 'v')
		{
		reg_init();

		char ***undo_mat = xmalloc(reg->rows * sizeof(char **));
		for (int i=0; i<reg->rows; i++)
			undo_mat[i] = xmalloc(reg->cols * sizeof(char *));
		char *current_ptr = reg->buff;
		for (int i = ch[0]; i < ch[1]; i++)
			{
			for (int j = ch[2]; j < ch[3]; j++)
				{
				undo_mat[i-ch[0]][j-ch[2]] = matrice->m[i][j];
				if (matrice->m[i][j] != NULL)
					{
					strcpy(current_ptr, matrice->m[i][j]);
					reg->m[i - ch[0]][j - ch[2]] = current_ptr;
					current_ptr += strlen(matrice->m[i][j]) + 1;
					}
				else reg->m[i - ch[0]][j - ch[2]] = NULL;
				matrice->m[i][j] = NULL;
				}
			}
		struct undo data[] = {{Delete, undo_mat, NULL, reg->rows, reg->cols, ch[0], ch[2], s_y, s_x, ch[0], ch[2]}};
		push(&uhead, data, 1);

		if (all_flag == 1) paste_flag = 1;
		else if (all_flag == 2) paste_flag = 2;
		else paste_flag = 0;
		visual_end();
		}
	else if (mode == 'n')
		{
		int key;
		key = getch();
		if (key == 'l' || key == KEY_RIGHT)
			{
			char *undo_cell = matrice->m[y][x];
			matrice->m[y][x] = NULL;
			struct undo data[] = {{DeleteCell, NULL, undo_cell, 0, 0, y, x, s_y, s_x, y, x}};
			push(&uhead, data, 1);
			}
		}
	}

void
paste_cells(const Arg *arg)
	{
	y_0 = y; x_0 = x;
	int loc_y = matrice->rows;
	int loc_x = matrice->cols;
	int add_y, add_x = 0;
	char *buffer = NULL;
	if (reg == NULL) return;
	else
		{
		buffer = xmalloc(reg->size * sizeof(char));
		memcpy(buffer, reg->buff, reg->size);
		}
	int rows = reg->rows;
	int cols = reg->cols;
	if (arg->i == PasteInverse)
		{
		rows = reg->cols;
		cols = reg->rows;
		if (paste_flag == 1) y = 0;
		else if (paste_flag == 2) x = 0;
		else if (paste_flag == 3)
			{ y = 0; loc_x = x; }
		else if (paste_flag == 4)
			{ x = 0; loc_y = y; }
		}
	else
		{
		if (paste_flag == 1) x = 0;
		else if (paste_flag == 2) y = 0;
		else if (paste_flag == 3)
			{ x = 0; loc_y = y; }
		else if (paste_flag == 4)
			{ y = 0; loc_x = x; }
		}

	if ((add_y = y + rows - matrice->rows) < 0) add_y = 0;
	if (paste_flag == 3 && arg->i == PasteNormal) add_y = rows;
	if (paste_flag == 4 && arg->i == PasteInverse) add_y = rows;
	if (add_y > 0) /* If not enough rows */
		{
		matrice->m = xrealloc(matrice->m, (matrice->rows + add_y) * sizeof(char **));
		for (int i = matrice->rows + add_y - 1; i >= loc_y + add_y; i--)
			matrice->m[i] = matrice->m[i - add_y];
		for (int i = 0; i < add_y; i++)
			{
			matrice->m[loc_y + i] = xmalloc(matrice->cols * sizeof(char *));
			for (int j = 0; j < matrice->cols; j++)
				matrice->m[loc_y + i][j] = NULL;
			}
		matrice->rows += add_y;
		}
	if ((add_x = x + cols - matrice->cols) < 0) add_x = 0;
	if (paste_flag == 4 && arg->i == PasteNormal) add_x = cols;
	if (paste_flag == 3 && arg->i == PasteInverse) add_x = cols;
	if (add_x > 0) /* If not enough cols */
		{
		for (int i = 0; i < matrice->rows; i++)
			{
			matrice->m[i] = xrealloc(matrice->m[i], (matrice->cols + add_x) * sizeof(char *));
			for (int j = matrice->cols + add_x - 1; j >= loc_x + add_x; j--)
				matrice->m[i][j] = matrice->m[i][j - add_x];
			for (int j = 0; j < add_x; j++)
				matrice->m[i][loc_x + j] = NULL;
			}
		matrice->cols += add_x;
		}
	char ***undo_mat = xmalloc(rows * sizeof(char **));
	char ***paste_mat = xmalloc(rows * sizeof(char **));
	for (int i=0; i<rows; i++)
		{
		undo_mat[i] = xmalloc(cols * sizeof(char *));
		paste_mat[i] = xmalloc(cols * sizeof(char *));
		}
	char *current_ptr = buffer;
	for (int i = 0; i < rows; i++)
		{
		for (int j = 0; j < cols; j++)
			{
			undo_mat[i][j] = matrice->m[y + i][x + j];
			char *inverse = (arg->i == PasteInverse) ? reg->m[j][i] : reg->m[i][j];
			if (inverse != NULL)
				{
				strcpy(current_ptr, inverse);
				paste_mat[i][j] = current_ptr;
				matrice->m[y + i][x + j] = current_ptr;
				current_ptr += strlen(inverse) + 1;
				}
			else
				{
				paste_mat[i][j] = NULL;
				matrice->m[y + i][x + j] = NULL;
				}
			}
		}
	struct undo data[] = {
		{Insert, NULL, NULL, add_y, add_x, y_0, x_0, s_y, s_x, loc_y, loc_x},
		{Delete, undo_mat, NULL, rows, cols, y_0, x_0, s_y, s_x, y, x},
		{Paste, paste_mat, buffer, rows, cols, y_0, x_0, s_y, s_x, y, x}
	};
	push(&uhead, data, 3);
	x = x_0;
	y = y_0;
	}

void
deleting()
	{
	if (mode == 'v')
		{
		if (ch[2] == 0 && ch[3] == matrice->cols)
			delete_row();
		else if (ch[0] == 0 && ch[1] == matrice->rows)
			delete_col();
		}
	else
		{
		int key;
		key = getch();
		y_0 = y;
		x_0 = x;
		if (key == 'l' || key == KEY_RIGHT)
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = x;
			ch[3] = x + 1;
			if (matrice->cols == 1) delete_row();
			else delete_col();
			}
		else if (key == '$')
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = x;
			ch[3] = matrice->cols;
			if (x == 0) delete_row();
			else delete_col();
			}
		else if (key == 'w')
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = x;
			ch[3] = x + MOVE_X;
			if (ch[3] > matrice->cols)
				ch[3] = matrice->cols;
			if (ch[3] - ch[2] == matrice->cols) delete_row();
			else delete_col();
			}
		else if (key == 'h' || key == KEY_LEFT)
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = x - 1;
			ch[3] = x;
			if (ch[2] < 0)
				ch[2] = 0;
			delete_col();
			}
		else if (key == '0')
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = 0;
			ch[3] = x;
			delete_col();
			}
		else if (key == 'b')
			{
			ch[0] = 0;
			ch[1] = matrice->rows;
			ch[2] = x - MOVE_X;
			ch[3] = x;
			if (ch[2] < 0)
				ch[2] = 0;
			delete_col();
			}
		else if (key == 'j' || key == KEY_DOWN)
			{
			ch[0] = y;
			ch[1] = y + 1;
			ch[2] = 0;
			ch[3] = matrice->cols;
			delete_row();
			}
		else if (key == 'G')
			{
			ch[0] = y;
			ch[1] = matrice->rows;
			ch[2] = 0;
			ch[3] = matrice->cols;
			delete_row();
			}
		else if (key == '\x04')
			{
			ch[0] = y;
			ch[1] = y + MOVE_Y;
			ch[2] = 0;
			ch[3] = matrice->cols;
			if (ch[1] > matrice->rows)
				ch[1] = matrice->rows;
			delete_row();
			}
		else if (key == 'k' || key == KEY_UP)
			{
			ch[0] = y - 1;
			ch[1] = y;
			ch[2] = 0;
			ch[3] = matrice->cols;
			delete_row();
			}
		else if (key == 'g')
			{
			ch[0] = 0;
			ch[1] = y;
			ch[2] = 0;
			ch[3] = matrice->cols;
			delete_row();
			}
		else if (key == '\x15')
			{
			ch[0] = y - MOVE_Y;
			ch[1] = y;
			ch[2] = 0;
			ch[3] = matrice->cols;
			if (ch[0] < 0)
				ch[0] = 0;
			delete_row();
			}
		}
	}

void
str_change(const Arg *arg)
	{
	if (mode == 'v') visual_end();
	mode = 'i';
	char *str;
	int rows = 0, cols = 0;
	while (mode == 'i' || mode == 'j')
		{
		if (y == matrice->rows)
			{
			matrice->rows++;
			matrice->m = xrealloc(matrice->m, matrice->rows * sizeof(char **));
			matrice->m[y] = xmalloc(matrice->cols * sizeof(char *));
			for (int j = 0; j < matrice->cols; j++)
				matrice->m[y][j] = NULL;
			rows = 1;
			cols = 0;
			}
		else if (x == matrice->cols)
			{
			matrice->cols++;
			for (int i = 0; i < matrice->rows; i++)
				{
				matrice->m[i] = xrealloc(matrice->m[i], matrice->cols * sizeof(char *));
				matrice->m[i][x] = NULL;
				}
			cols = 1;
			rows = 0;
			}
		if (arg->i == 0)
			str = get_str("", 0, 0);
		else if (arg->i == 1)
			str = get_str(matrice->m[y][x], 0, 0);
		else if (arg->i == 2)
			str = get_str(matrice->m[y][x], 1, 0);
		char *undo_cell = matrice->m[y][x];
		char *paste_cell = str;
		matrice->m[y][x] = str;

		struct undo data[] = {
			{Insert, NULL, NULL, rows, cols, y, x, s_y, s_x, y, x},
			{DeleteCell, NULL, undo_cell, rows, cols, y, x, s_y, s_x, y, x},
			{PasteCell, NULL, paste_cell, rows, cols, y, x, s_y, s_x, y, x}
		};
		push(&uhead, data, 3);
		if (mode == 'i')
			y++;
		if (mode == 'j')
			x++;
		}
	}

void
push(node_t **uhead, struct undo *data, int dc)
	{
	node_t *new_node = xmalloc(sizeof(node_t));
	new_node->data = xmalloc(dc * sizeof(struct undo));;
	for (int i = 0; i < dc; i++)
		new_node->data[i] = data[i];
	new_node->dc = dc;
	new_node->prev = *uhead;
	new_node->next = NULL;

	/* free previous nodes if in the middle of history */
	while ((*uhead)->next != NULL)
		{
		node_t *temp = (*uhead)->next;
		for (int i = 0; i < temp->dc; i++)
			{
			if (temp->data[i].mat != NULL)
				free_matrix(&(temp->data[i].mat), temp->data[i].rows, temp->data[i].cols);
			if (temp->data[i].cell != NULL)
				{
				if (temp->data[i].operation == PasteCell || temp->data[i].operation == Paste)
					free(temp->data[i].cell);
				}
			}
		free(temp->data);
		(*uhead)->next = temp->next;
		free(temp);
		}

	(*uhead)->next = new_node;
	*uhead = new_node;
	}

void
undo(const Arg *arg)
	{
	if (uhead != NULL)
		{
		if (arg->i == Redo && uhead->next == NULL) return;
		else if (arg->i == Undo && uhead->prev == NULL) return;
		if (arg->i == Redo) uhead = uhead->next;
		for (int m = 0; m < uhead->dc; m++)
			{
			int l;
			if (arg->i == Undo) l = uhead->dc - 1 - m;
			else l = m;
			int op = uhead->data[l].operation;
			if (arg->i == Undo)
				{
				if (op == Delete) op = Paste;
				else if (op == Paste) op = Delete;
				else if (op == Cut) op = Insert;
				else if (op == Insert) op = Cut;
				else if (op == PasteCell) op = DeleteCell;
				else if (op == DeleteCell) op = PasteCell;
				}
			if (op == Delete)
				{
				for (int i = 0; i < uhead->data[l].rows; i++)
					{
					for (int j = 0; j < uhead->data[l].cols; j++)
						{
						if (uhead->data[l].mat[i][j] != NULL)
							matrice->m[uhead->data[l].loc_y + i][uhead->data[l].loc_x + j] = NULL;
						}
					}
				}
			else if (op == Paste)
				{
				for (int i = 0; i < uhead->data[l].rows; i++)
					{
					for (int j = 0; j < uhead->data[l].cols; j++)
						{
						if (uhead->data[l].mat[i][j] != NULL)
							matrice->m[uhead->data[l].loc_y + i][uhead->data[l].loc_x + j] = uhead->data[l].mat[i][j];
						}
					}
				}
			else if (op == PasteCell)
				matrice->m[uhead->data[l].loc_y][uhead->data[l].loc_x] = uhead->data[l].cell;
			else if (op == DeleteCell)
				matrice->m[uhead->data[l].loc_y][uhead->data[l].loc_x] = NULL;
			else if (op == Cut)
				{
				if (uhead->data[l].rows > 0)
					{
					int num = uhead->data[l].rows;
					for (int i = 0; i < num; i++) {
						free(matrice->m[uhead->data[l].loc_y + i]);
					}
					for (int i = uhead->data[l].loc_y; i < matrice->rows - num; i++)
						matrice->m[i] = matrice->m[i + num];
					matrice->m = xrealloc(matrice->m, (matrice->rows - num) * sizeof(char **));
					matrice->rows -= num;
					}
				if (uhead->data[l].cols > 0)
					{
					int num = uhead->data[l].cols;
					for (int j = 0; j < matrice->rows; j++)
						{
						for (int i = uhead->data[l].loc_x; i < matrice->cols - num; i++)
							matrice->m[j][i] = matrice->m[j][i + num];
						matrice->m[j] = xrealloc(matrice->m[j], (matrice->cols - num) * sizeof(char *));
						}
					matrice->cols -= num;
					}
				}
			else if (op == Insert)
				{
				if (uhead->data[l].cols > 0)
					{
					for (int i = 0; i < matrice->rows; i++)
						{
						matrice->m[i] = xrealloc(matrice->m[i], (matrice->cols + uhead->data[l].cols) * sizeof(char *));
						for (int j = matrice->cols + uhead->data[l].cols - 1; j >= uhead->data[l].loc_x + uhead->data[l].cols; j--)
							matrice->m[i][j] = matrice->m[i][j - uhead->data[l].cols];
						for (int j = 0; j < uhead->data[l].cols; j++)
							matrice->m[i][uhead->data[l].loc_x + j] = NULL;
						}
					matrice->cols += uhead->data[l].cols;
					}
				if (uhead->data[l].rows > 0)
					{
					matrice->m = xrealloc(matrice->m, (matrice->rows + uhead->data[l].rows) * sizeof(char **));
					for (int i = matrice->rows + uhead->data[l].rows - 1; i >= uhead->data[l].loc_y + uhead->data[l].rows; i--)
						matrice->m[i] = matrice->m[i - uhead->data[l].rows];
					for (int i = 0; i < uhead->data[l].rows; i++)
						{
						matrice->m[uhead->data[l].loc_y + i] = xmalloc(matrice->cols * sizeof(char *));
						for (int j = 0; j < matrice->cols; j++)
							matrice->m[uhead->data[l].loc_y + i][j] = NULL;
						}
					matrice->rows += uhead->data[l].rows;
					}
				}
			if (uhead->data[l].y == matrice->rows)
				y = uhead->data[l].y - 1;
			else
				y = uhead->data[l].y;
			if (uhead->data[l].x == matrice->cols)
				x = uhead->data[l].x - 1;
			else
				x = uhead->data[l].x;
			s_y = uhead->data[l].s_y;
			s_x = uhead->data[l].s_x;
			}
		if (arg->i == Undo) uhead = uhead->prev;
		}
	}

void
die(void)
	{
	endwin();
	if (uhead)
		{
		while (uhead->next != NULL)
			uhead = uhead->next;
		while (1)
			{
			if (uhead->prev == NULL)
				break;
			for (int i = 0; i < uhead->dc; i++)
				{
				if (uhead->data[i].mat != NULL)
					{
					free_matrix(&(uhead->data[i].mat), uhead->data[i].rows, uhead->data[i].cols);
					}
				if (uhead->data[i].cell != NULL)
					{
					if (uhead->data[i].operation == PasteCell || uhead->data[i].operation == Paste)
						free(uhead->data[i].cell);
					}
				}
			free(uhead->data);
			uhead = uhead->prev;
			free(uhead->next);
			}
		}
	free_matrix(&matrice->m, matrice->rows, matrice->cols);
	free(matrice->buff);
	free(matrice);
	if (reg)
		{
		free_matrix(&reg->m, reg->rows, reg->cols);
		free(reg->buff);
		free(reg);
		}
	free(fname);
	unlink(FIFO);
	printf("\e]0;\a");
	fflush(stdout);
	}

void
quit()
	{
	die();
	exit(0);
	}

void
keypress(int key)
	{
	static int key0 = -1;
	static int i0 = 0;
	for (int i = i0; i < sizeof(keys)/sizeof(keys[0]); i++)
		{
		if (key == keys[i].key[0] && key0 == -1)
			{
			if (keys[i].key[1] == -1)
				{
				(*keys[i].func)(&keys[i].arg);
				key0 = -1;
				i0 = 0;
				}
			else
				{
				key0 = key;
				i0 = i;
				return;
				}
			}
		else if (key0 == keys[i].key[0] && key == keys[i].key[1])
			{
			(*keys[i].func)(&keys[i].arg);
			key0 = -1;
			i0 = 0;
			}
		}
	key0 = -1;
	i0 = 0;
	}

char ***
write_to_matrix(char **buffer, int *n_rows, int *n_cols)
	{
	int row = 0, col = 0;
	int col_s = 32, row_s = 32;
	size_t n = 0;
	int f = 0;

	char ***matrix = xmalloc(row_s * sizeof(char **));
	matrix[row] = xmalloc(col_s * sizeof(char *));
	char *k = *buffer;
	char *start = k;
	while (*k)
		{
		n++;
		if (*k == fs)
			{
			if (col >= col_s)
				{
				col_s *= 2;
				matrix[row] = xrealloc(matrix[row], col_s * sizeof(char *));
				}
			*k = '\0'; 
			matrix[row][col] = start;
			n = 0;
			col++;
			start = k + 1;
			}
		else if (*k == '\n')
			{
			*k = '\0';
			matrix[row][col] = start;
			n = 0;
			col++;
			if (col > f) /* If row more columns than previous add cols to rows before */
				{
				for (int i = 0; i < row; i++)
					{
					matrix[i] = xrealloc(matrix[i], col * sizeof(char *));
					for (int j = f; j < col; j++)
						matrix[i][j] = NULL;
					}
				f = col;
				}
			while (col < f) /* If row less columns than previous add cols to n_cols */
				matrix[row][col++] = NULL;
			col = 0;
			matrix[row] = xrealloc(matrix[row], f * sizeof(char *));
			row++;
			if (row >= row_s)
				{
				row_s *= 2;
				matrix = xrealloc(matrix, row_s * sizeof(char **));
				}
			start = k + 1;
			matrix[row] = xmalloc(col_s * sizeof(char *));
			}
		k++;
		}

	if (n == 0 && col == 0) free(matrix[row]);
	else
		{
		if (n)
			{ matrix[row][col] = xstrdup(start); col++; }
		if (col > f)
			{
			for (int i = 0; i < row; i++)
				{
				matrix[i] = xrealloc(matrix[i], col * sizeof(char *));
				for (int j = f; j < col; j++)
					matrix[i][j] = NULL;
				}
			f = col;
			}
		while (col < f)
			matrix[row][col++] = NULL;
		row++;
		}

	*n_rows = row;
	*n_cols = f;

	if (*n_rows == 0 || *n_cols == 0) return NULL;
	matrix = xrealloc(matrix, *n_rows * sizeof(char **));

	return matrix;
	}

void
free_matrix(char ****matrix, int n_rows, int n_cols)
	{
	for (int i = 0; i < n_rows; i++)
		free((*matrix)[i]);
	free(*matrix);
	}

void
init_ui(void)
	{
	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	raw();
	noecho();
	keypad(stdscr, TRUE);
	}

void
usage(void)
	{
	fprintf(stderr, "Uporaba: %s [-f separator] [file]\n", argv0);
	exit(EXIT_FAILURE);
	}

int
main(int argc, char *argv[])
	{
	FILE *file = NULL;
	ARGBEGIN
		{
		case 'f':
			fs = EARGF(usage())[0];
			break;
		default:
			usage();
		}
	ARGEND;
	if (argc > 0)
		{
		fname = strdup(argv[0]);
		if (fname == NULL) exit(EXIT_FAILURE);
		printf("\e]0;%s - csvis\a", fname);
		fflush(stdout);
		file = fopen(fname, "r");
		}
	else
		{
		fname = NULL;
		printf("\e]0;[No name] - csvis\a");
		fflush(stdout);
		}
	matrice = malloc(sizeof(struct Mat));
	if (matrice == NULL)
		{
		free(fname);
		exit(EXIT_FAILURE);
		}
	readall(file, &matrice->buff, &matrice->size);
	matrice->m = write_to_matrix(&matrice->buff, &matrice->rows, &matrice->cols);
	uhead = xmalloc(sizeof(node_t));
	uhead->next = NULL;
	uhead->prev = NULL;

	if (file != NULL)
		fclose(file);

	init_ui();
	int key;

	if (mkfifo(FIFO, 0666) == -1)
		{
		if (errno != EEXIST)
			statusbar("Error with mkfifo, pipe to named pipe will not be possible.");
		}
	while (1)
		{
		when_resize();
		draw();
		key = getch();
		keypress(key);
		}
	}

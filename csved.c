#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

#define MAX_ROWS 100
#define MAX_COLS 100
#define MAX_CELL_WIDTH 5

char ***matrix;
int num_rows, num_cols;
int rows, cols;
int y, x = 0;
int c_y, c_x = 0;
int v_y, v_x = 0;
int s_y, s_x = 0;
int c_y0, c_x0 = 0;
int s_y0, s_x0 = 0;
int ch[4] = {0, 0, 0, 0};
char mode = 'n';
int scr_x, scr_y;

typedef union {
	int i;
} Arg;

typedef struct {
	int key;
	void (*func)(const Arg *arg);
	const Arg arg;
} Key;

void calc_ch(int *list, int y, int x, int v_y, int v_x) {
    if (v_y >= 0 && v_x >= 0) {
		list[0] = y;
		list[1] = y+1+v_y;
		list[2] = x;
		list[3] = x+1+v_x;
	}
	else if (v_y >= 0 && v_x < 0) {
        list[0] = y;
		list[1] = y+1+v_y;
		list[2] = x+v_x;
		list[3] = x+1;
	}
	else if (v_y < 0 && v_x >= 0) {
        list[0] = y+v_y;
		list[1] = y+1;
		list[2] = x;
		list[3] = x+1+v_x;
	}
	else if (v_y < 0 && v_x < 0) {
        list[0] = y+v_y;
		list[1] = y+1;
		list[2] = x+v_x;
		list[3] = x+1;
	}
}

void draw() {
	clear();
	char cell_str[MAX_CELL_WIDTH + 1];
	for (int i = 0; i < scr_y; i++) {
		for (int j = 0; j < scr_x && matrix[i][j] != NULL; j++) {
			snprintf(cell_str, MAX_CELL_WIDTH + 1, "%-10s", matrix[i + s_y][j + s_x]);
			if (ch[0] <= s_y + i && s_y + i < ch[1] && ch[2] <= s_x + j && s_x + j < ch[3]) {
				attron(A_STANDOUT);
			}
			else attroff(A_STANDOUT);
			mvprintw(i, j * MAX_CELL_WIDTH, "%s", cell_str);
		}
	}
	wmove(stdscr, c_y, c_x);
}

void move_down(const Arg *arg) {
	if (y + v_y < num_rows - arg->i && arg->i != 0) {
		if (mode == 'v') {
			v_y = v_y + arg->i;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else y = y + arg->i;
	}
	else {
		if (mode == 'v') {
			v_y = num_rows - 1 - y;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else y = num_rows - 1;
	}
	if (c_y < scr_y - arg->i && arg->i != 0)
		c_y = y + v_y - s_y;
	else {
		c_y = scr_y - 1;
		s_y = y + v_y - (scr_y - 1);
	}
}

void move_up(const Arg *arg) {
	if (y + v_y >= arg->i && arg->i != 0) {
		if (mode == 'v') {
			v_y = v_y - arg->i;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else y = y - arg->i;
	}
	else {
		if (mode == 'v') {
			v_y = 0 - y;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else y = 0;
	}
	if (c_y >= arg->i && arg->i != 0)
		c_y = y + v_y - s_y;
	else {
		c_y = 0;
		s_y = y + v_y;
	}
}

void move_right(const Arg *arg) {
	if (x + v_x < num_cols - arg->i && arg->i != 0) {
		if (mode == 'v') {
			v_x = v_x + arg->i;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else x = x + arg->i;
	}
	else {
		if (mode == 'v') {
			v_x = num_cols - 1 - x;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else x = num_cols - 1;
	}
	if (c_x < (scr_x - arg->i)*MAX_CELL_WIDTH && arg->i != 0)
		c_x = (x + v_x - s_x)*MAX_CELL_WIDTH;
	else {
		c_x = (scr_x - 1)*MAX_CELL_WIDTH;
		s_x = x + v_x - (scr_x - 1);
	}
}

void move_left(const Arg *arg) {
	if (x + v_x >= arg->i && arg->i != 0) {
		if (mode == 'v') {
			v_x = v_x - arg->i;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else x = x - arg->i;
	}
	else {
		if (mode == 'v') {
			v_x = 0 - x;
			calc_ch(ch, y, x, v_y, v_x);
		}
		else x = 0;
	}
	if (c_x >= arg->i && arg->i != 0)
		c_x = (x + v_x - s_x)*MAX_CELL_WIDTH;
	else {
		c_x = 0;
		s_x = x + v_x;
	}
}

void when_resize() {
	getmaxyx(stdscr, rows, cols);
	if (num_cols*MAX_CELL_WIDTH < cols) {
		scr_x = num_cols;
	}
	else scr_x = cols/MAX_CELL_WIDTH;
	if (num_rows < rows)  {
		scr_y = num_rows;
	}
	else scr_y = rows;
}

void insert_col(const Arg *arg) {
	for (int i = 0; i < num_rows; i++) {
		matrix[i] = (char **)realloc(matrix[i], (num_cols+1) * sizeof(char *));
		for (int j = num_cols; j > x + arg->i; j--) {
			matrix[i][j] = matrix[i][j - 1];
		}
		matrix[i][x + arg->i] = strdup("");
	}
	num_cols++;
	if (num_cols*MAX_CELL_WIDTH < cols) {
		scr_x = num_cols;
	}
	else scr_x = cols/MAX_CELL_WIDTH;
	x = x + arg->i;
	if (c_x < (scr_x - arg->i)*MAX_CELL_WIDTH)
		c_x = (x + v_x - s_x)*MAX_CELL_WIDTH;
	else {
		c_x = (scr_x - 1)*MAX_CELL_WIDTH;
		s_x = x + v_x - (scr_x - 1);
	}
}

void insert_row(const Arg *arg) {
	for (int i = num_rows; i > y + arg->i; i--) {
		matrix[i] = matrix[i - 1];
	}
	num_rows++;
	matrix[y + arg->i] = (char **)malloc(num_cols * sizeof(char *));
	for (int j = 0; j < num_cols; j++) {
		matrix[y + arg->i][j] = strdup("");
	}
	if (num_rows < rows)  {
		scr_y = num_rows;
	}
	else scr_y = rows;
	y = y + arg->i;
	if (c_y < scr_y - arg->i)
		c_y = y + v_y - s_y;
	else {
		c_y = scr_y - 1;
		s_y = y + v_y - (scr_y - 1);
	}
}

char* get_str(char *str0, char *str1) {
    int key;
    int i = strlen(str0);
    int bufsize = 10; // Initial buffer size
    char *buffer = malloc(strlen(str0) + strlen(str1) + bufsize * sizeof(char));
	//wcscpy(buffer, str0);
	strcpy(buffer, str0);
	addstr(str0);
	addstr(str1);
	c_x = c_x + i;
	wmove(stdscr, c_y, c_x);

    while (1) {
        key = getch();
		if (key == '\x03')
			break;
		else if (key == '\n')
			break;
        else if (key == KEY_BACKSPACE) {
			if (i > 0) {
				c_x--;
				buffer[--i] = '\0';
				mvaddstr(c_y, c_x, str1);
				addch(' ');
				wmove(stdscr, c_y, c_x);
            }
        }
		else {
            if (i == bufsize - 1) {
                bufsize *= 2;
                buffer = realloc(buffer, bufsize * sizeof(char));
            }
			if (key == '\t') continue; // ignore tab
            buffer[i++] = key;
			c_x++;
			addch(key);
			addstr(str1);
			wmove(stdscr, c_y, c_x);
        }
    }

	for (int j=0; j<strlen(str1); j++)
		buffer[i+j] = *(str1+j);
    buffer[i + strlen(str1)] = '\0'; // Null-terminate the string
	c_x = (x + v_x - s_x)*MAX_CELL_WIDTH;
	if (key == '\x03') {
		free(buffer);
		buffer = strdup("");
	}
    return buffer;
}

void str_change() {
	char cell_str[MAX_CELL_WIDTH + 1];
	snprintf(cell_str, MAX_CELL_WIDTH + 1, "%-10s", "");
	addstr(cell_str);
	wmove(stdscr, c_y, c_x);
	strcpy(matrix[y][x], get_str("", ""));
}

void str_append() {
	char cell_str[MAX_CELL_WIDTH + 1];
	snprintf(cell_str, MAX_CELL_WIDTH + 1, "%-10s", "");
	wmove(stdscr, c_y, c_x);
	//strcpy(matrix[y][x], get_str(matrix[y][x], ""));
	matrix[y][x] = strdup(get_str(matrix[y][x], ""));
}

void str_insert() {
	char cell_str[MAX_CELL_WIDTH + 1];
	snprintf(cell_str, MAX_CELL_WIDTH + 1, "%-10s", "");
	wmove(stdscr, c_y, c_x);
	//strcpy(matrix[y][x], get_str(matrix[y][x], ""));
	matrix[y][x] = strdup(get_str("", matrix[y][x]));
}

void quit() {
	endwin();
	for (int i = 0; i < num_rows; i++) {
		for (int j = 0; j < num_cols && matrix[i][j] != NULL; j++) {
			free(matrix[i][j]);
		}
		free(matrix[i]);
	}
	free(matrix);
	exit(0);
}

void visual_start() {
	mode = 'v';
	c_y0 = c_y;
	c_x0 = c_x;
	s_y0 = s_y;
	s_x0 = s_x;
	ch[0] = y;
	ch[1] = y + 1;
	ch[2] = x;
	ch[3] = x + 1;
}
 
void visual_end() {
	mode = 'n';
	v_y = 0;
	v_x = 0;
	ch[0], ch[1], ch[2], ch[3] = 0;
	c_y = c_y0;
	c_x = c_x0;
	s_y = s_y0;
	s_x = s_x0;
}

static Key keys[] = {
	{'q', quit, {0}},
	{'v', visual_start, {0}},
	{'\x1b', visual_end, {0}},
	{'j', move_down, {.i = 1}},
	{KEY_DOWN, move_down, {.i = 1}},
	{'k', move_up, {.i = 1}},
	{KEY_UP, move_up, {.i = 1}},
	{'l', move_right, {.i = 1}},
	{KEY_RIGHT, move_right, {.i = 1}},
	{'h', move_left, {.i = 1}},
	{KEY_LEFT, move_left, {.i = 1}},
	{'\x04', move_down, {.i = 3}},
	{'\x15', move_up, {.i = 3}},
	{'w', move_right, {.i = 3}},
	{'b', move_left, {.i = 3}},
	{'G', move_down, {.i = 0}},
	{'g', move_up, {.i = 0}},
	{'$', move_right, {.i = 0}},
	{'0', move_left, {.i = 0}},
	{'c', str_change, {0}},
	{'a', str_append, {0}},
	{'i', str_insert, {0}},
	{'O', insert_row, {0}},
	{'o', insert_row, {1}},
	{'I', insert_col, {0}},
	{'A', insert_col, {1}},
	{KEY_RESIZE, when_resize, {0}}
};

void keypress(int key) {
	for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		if (key == keys[i].key)
			(*keys[i].func)(&keys[i].arg);
	}
}

char ***read_to_matrix(FILE *file, int *num_rows, int *num_cols) {
	char ***matrix = malloc(100*sizeof(char **));
	char *line_buf = NULL;
	size_t line_buf_size = 0;
	ssize_t line_size;
	char *token;
	*num_rows = 0;

	while (line_size = getline(&line_buf, &line_buf_size, file) >= 0) {
		*num_cols = 0;
		line_buf[strcspn(line_buf, "\n")] = '\0';
		token = strtok(line_buf, ",");
		matrix[*num_rows] = malloc(100 * sizeof(char *));
		if (matrix[*num_rows] == NULL) {
			perror("Napaka pri dodeljevanju pomnilnika");
		}
		matrix[*num_rows][*num_cols] = strdup(token);
		(*num_cols)++;
		while ((token = strtok(NULL, ",")) != NULL) {
			matrix[*num_rows][*num_cols] = strdup(token);
			(*num_cols)++;
		}
		(*num_rows)++;
		free(line_buf);
		line_buf = NULL;
	}

	return matrix;
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
	int error;
    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        perror("Napaka pri odpiranju datoteke");
        error = 1;
    }
	matrix = read_to_matrix(file, &num_rows, &num_cols);
	fclose(file);

    initscr();
    cbreak();
	raw();
    noecho();
	set_tabsize(MAX_CELL_WIDTH);
    keypad(stdscr, TRUE); // omogoči uporabo funkcij, kot so KEY_LEFT
	int key;
	//wrefresh();
	int h, w;
	int step_mv = 3;

	when_resize();

	while (1) {
		draw();
		key = getch();
		keypress(key);
	}

    endwin();
    for (int i = 0; i < num_rows; i++) {
        for (int j = 0; j < num_cols && matrix[i][j] != NULL; j++) {
            free(matrix[i][j]);
        }
        free(matrix[i]);
    }
    free(matrix);

    return 0;
}


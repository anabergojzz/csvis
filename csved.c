// check if memory can be freed anywhere
// memory realloc fo cols and rows
// chack if visual can be changed to simplify movement functions
// utf8 chars format to cell width
#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>

#define MAX_ROWS 10000
#define MAX_COLS 10000
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
	const char *filename;
} Arg;

typedef struct {
	int key;
	void (*func)(const Arg *arg);
	const Arg arg;
} Key;

size_t utf8_strlen(const char *str) {
    mbstate_t state = {0};
    const char *s = str;
    size_t len = 0;
    while (*s) {
        size_t ret = mbrlen(s, MB_CUR_MAX, &state);
        if (ret == (size_t)-1 || ret == (size_t)-2) {
            break;
        }
        s += ret;
        len++;
    }
    return len;
}

void write_csv(const Arg *arg) {
    FILE *file = fopen(arg->filename, "w");
    if (!file) {
        perror("Error opening file for writing");
        exit(EXIT_FAILURE);
    }

	for (int i = 0; i < num_rows; i++) {
		for (int j = 0; j < num_cols-1; j++) {
			fprintf(file, "%s", matrix[i][j]);
			fprintf(file, ",");
		}
		fprintf(file, "%s", matrix[i][num_cols-1]);
		fprintf(file, "\n");
	}

    fclose(file);
}

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
	for (int i = 0; i < scr_y; i++) {
		for (int j = 0; j < scr_x && matrix[i][j] != NULL; j++) {
			if (ch[0] <= s_y + i && s_y + i < ch[1] && ch[2] <= s_x + j && s_x + j < ch[3]) {
				attron(A_STANDOUT);
			}
			else attroff(A_STANDOUT);
			mvprintw(i, j * MAX_CELL_WIDTH, "%.*s", MAX_CELL_WIDTH-1, matrix[i + s_y][j + s_x]);
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
	if ((num_cols - s_x)*MAX_CELL_WIDTH < cols) {
		scr_x = num_cols - s_x;
	}
	else scr_x = cols/MAX_CELL_WIDTH;
	if (num_rows - s_y < rows)  {
		scr_y = num_rows - s_y;
	}
	else scr_y = rows;
	if (c_y < scr_y)
		c_y = y + v_y - s_y;
	else {
		c_y = scr_y - 1;
		s_y = y + v_y - (scr_y - 1);
	}
	if (c_x < scr_x*MAX_CELL_WIDTH)
		c_x = (x + v_x - s_x)*MAX_CELL_WIDTH;
	else {
		c_x = (scr_x - 1)*MAX_CELL_WIDTH;
		s_x = x + v_x - (scr_x - 1);
	}
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

char* get_str(char* str, char loc) {
	ssize_t str_size = strlen(str);
    ssize_t bufsize = str_size + 10; // Initial buffer size
    char* buffer = (char*) malloc(bufsize * sizeof(char));
    if (buffer == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
	strcpy(buffer, str);
	int i = 0;
	int i_utf8 = 0;
	if (loc == 1) {
		i = str_size;
		i_utf8 += utf8_strlen(str);
	}
	int cx_add, cy_add = 0;

	int key;
	char k = 0; // to track multibyte utf8 chars

    while (1) {
        if (str_size >= bufsize - 4) { // If buffer is nearly full, increase its size
            bufsize += 10;
            char* new_buffer = (char*)realloc(buffer, bufsize * sizeof(char));
            if (new_buffer == NULL) {
                free(buffer);
                fprintf(stderr, "Memory reallocation failed\n");
                exit(1);
            }
            buffer = new_buffer;
        }
		if (k > 0) k--;
		else {
			draw();
			mvprintw(c_y, c_x, "%*s", MAX_CELL_WIDTH, ""); // clear cell
			mvaddstr(c_y, c_x, buffer);
			addch(' ');
			if ((cols - c_x) > i_utf8) {
				cx_add = i_utf8;
				cy_add = 0;
			}
			else {
				cx_add = (i_utf8 - (cols - c_x))%cols - c_x;
				cy_add = 1 + (i_utf8 - (cols - c_x))/cols;
			}
			wmove(stdscr, c_y + cy_add, c_x + cx_add);
		}
        key = getch();
		if (key == '\n') {
			break;
		}
		else if (key == KEY_LEFT) {
			if (i > 0) {
				while ((buffer[--i] & 0xC0) == 0x80) {
					continue;
				}
				i_utf8--;
			}
		}
		else if (key == KEY_RIGHT) {
			if (i < str_size) {
				while ((buffer[++i] & 0xC0) == 0x80) {
					continue;
				}
				i_utf8++;
			}
		}
        else if (key == KEY_BACKSPACE) {
			if (i > 0) {
				i_utf8--;
				while ((buffer[--i] & 0xC0) == 0x80) {
					str_size--;
					strcpy(buffer + i, buffer + i + 1);
				}
				str_size--;
				strcpy(buffer + i, buffer + i + 1);
            }
        }
		else if (key == KEY_RESIZE) {
			when_resize();
		}
		else {
			strcpy(buffer + i + 1, buffer + i);
			if (key < 256) {
				buffer[i] = (char)key;
				if ((buffer[i] & 0xC0) != 0x80) i_utf8++;
				if ((buffer[i] & 0xC0) == 0xC0) k = 1;
				i++;
				str_size++;
			}
        }
    }

	return buffer;
}

void str_change() {
	matrix[y][x] = strdup(get_str("", 0));
}

void str_append() {
	matrix[y][x] = strdup(get_str(matrix[y][x], 1));
}

void str_insert() {
	matrix[y][x] = strdup(get_str(matrix[y][x], 0));
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
	if (mode != 'v') {
		mode = 'v';
		c_y0 = c_y;
		c_x0 = c_x;
		s_y0 = s_y;
		s_x0 = s_x;
		// could add x0 = x and simplify all movement functions
		ch[0] = y;
		ch[1] = y + 1;
		ch[2] = x;
		ch[3] = x + 1;
	}
	else {
		mode = 'n';
		v_y = 0;
		v_x = 0;
		ch[0], ch[1], ch[2], ch[3] = 0;
		c_y = c_y0;
		c_x = c_x0;
		s_y = s_y0;
		s_x = s_x0;
	}
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
	{ 's', write_csv, { .filename = NULL} }, // filename will be set at runtime
};

void keypress(int key, Arg targ) {
	for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		if (key == keys[i].key) {
			if (keys[i].func == write_csv) (*keys[i].func)(&targ);
			else (*keys[i].func)(&keys[i].arg);
		}
	}
}

char ***read_to_matrix(FILE *file, int *num_rows, int *num_cols) {
	char ***matrix = (char ***) malloc(MAX_ROWS*sizeof(char **));
	char *line_buf = NULL;
	size_t line_buf_size = 0;
	ssize_t line_size;
	char *token;
	*num_rows = 0;

	while (line_size = getline(&line_buf, &line_buf_size, file) >= 0) {
		*num_cols = 0;
		line_buf[strcspn(line_buf, "\n")] = '\0';
		matrix[*num_rows] = (char **) malloc(MAX_COLS * sizeof(char *));
		if (matrix[*num_rows] == NULL) {
			perror("Napaka pri dodeljevanju pomnilnika");
		}
		if (line_buf[0] == ',') {
			matrix[*num_rows][*num_cols] = strdup("");
			(*num_cols)++;
		}
		token = strtok(line_buf, ",");
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
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    setlocale(LC_ALL, "");
	int error;
	const char *filename = argv[1];
	const Arg targ = {.filename = filename}; //just to pass filename to write_csv func
    FILE *file = fopen(filename, "r");
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
	wchar_t key;
	//wrefresh();
	int h, w;
	int step_mv = 3;

	while (1) {
		when_resize();
		draw();
		key = getch();
		keypress(key, targ);
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


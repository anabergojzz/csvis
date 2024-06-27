#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <string.h>
//#include <wchar.h>
#include <locale.h>

#define CELL_WIDTH 10

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

void draw() {
	clear();
	for (int i = 0; i < scr_y; i++) {
		for (int j = 0; j < scr_x; j++) {
			if (ch[0] <= s_y + i && s_y + i < ch[1] && ch[2] <= s_x + j && s_x + j < ch[3]) {
				attron(A_STANDOUT);
			}
			else attroff(A_STANDOUT);
			char* xy = matrix[i + s_y][j + s_x];
			int utf8_w = 0;
			for (int k = 0; k < CELL_WIDTH*4; k++) {
				if ((xy[k] & 0xC0) != 0xC0) utf8_w++;
				if (utf8_w == CELL_WIDTH) {
					utf8_w = k;
					break;
				}
			}
			mvprintw(i, j * CELL_WIDTH, "%.*s", utf8_w, xy);
		}
	}
	wmove(stdscr, c_y, c_x);
}

void move_down(const Arg *arg) {
	if (y < num_rows - arg->i && arg->i != 0) {
		if (mode == 'v') {
			if (ch[0] != y)
				ch[1] = y + arg->i + 1;
			else if ((ch[0] + arg->i) <= v_y)
				ch[0] += arg->i;
			else {
				ch[0] = v_y;
				ch[1] += arg->i - (v_y - ch[0]);
			}
		}
		y = y + arg->i;
	}
	else {
		if (mode == 'v') {
			ch[1] = num_rows;
			ch[0] = v_y;
		}
		y = num_rows - 1;
	}
	if (c_y >= scr_y - arg->i || arg->i == 0)
		s_y = y - (scr_y - 1);
}

void move_up(const Arg *arg) {
	if (y >= arg->i && arg->i != 0) {
		if (mode == 'v') {
			if (ch[0] != y)
				ch[1] = y - arg->i + 1;
			else if (ch[1] == v_y + 1)
				ch[0] -= arg->i;
			else {
				ch[1] = v_y + 1;
				ch[0] -= arg->i - (ch[1] - v_y - 1);
			}
		}
		y = y - arg->i;
	}
	else {
		if (mode == 'v') {
			ch[0] = 0;
			ch[1] = v_y + 1;
		}
		y = 0;
	}
	if (c_y < arg->i || arg->i == 0)
		s_y = y;
}

void move_right(const Arg *arg) {
	if (x < num_cols - arg->i && arg->i != 0) {
		if (mode == 'v') {
			if (ch[2] != x)
				ch[3] = x + arg->i + 1;
			else if ((ch[2] + arg->i) <= v_x)
				ch[2] += arg->i;
			else {
				ch[2] = v_x;
				ch[3] += arg->i - (v_x - ch[2]);
			}
		}
		x += arg->i;
	}
	else {
		if (mode == 'v') {
			ch[3] = num_cols;
			ch[2] = v_x;
		}
		x = num_cols - 1;
	}
	if (c_x >= (scr_x - arg->i)*CELL_WIDTH || arg->i == 0)
		s_x = x - (scr_x - 1);
}

void move_left(const Arg *arg) {
	if (x >= arg->i && arg->i != 0) {
		if (mode == 'v') {
			if (ch[2] != x)
				ch[3] = x - arg->i + 1;
			else if (ch[3] == v_x + 1)
				ch[2] -= arg->i;
			else {
				ch[3] = v_x + 1;
				ch[2] -= arg->i - (ch[3] - v_x - 1);
			}
		}
		x -= arg->i;
	}
	else {
		if (mode == 'v') {
			ch[2] = 0;
			ch[3] = v_x + 1;
		}
		x = 0;
	}
	if (c_x < (arg->i)*CELL_WIDTH || arg->i == 0)
		s_x = x;
}

void when_resize() {
	getmaxyx(stdscr, rows, cols);
	if ((num_cols - s_x)*CELL_WIDTH < cols) {
		scr_x = num_cols - s_x;
	}
	else scr_x = cols/CELL_WIDTH;
	if (num_rows - s_y < rows)  {
		scr_y = num_rows - s_y;
	}
	else scr_y = rows;
	if (c_y < scr_y)
		c_y = y - s_y;
	else {
		c_y = scr_y - 1;
		s_y = y - (scr_y - 1);
	}
	if (scr_x == 0);
	else if (c_x < scr_x*CELL_WIDTH)
		c_x = (x - s_x)*CELL_WIDTH;
	else {
		c_x = (scr_x - 1)*CELL_WIDTH;
		s_x = x - (scr_x - 1);
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
	if (num_cols*CELL_WIDTH < cols) {
		scr_x = num_cols;
	}
	else scr_x = cols/CELL_WIDTH;
	x = x + arg->i;
	if (c_x < (scr_x - arg->i)*CELL_WIDTH)
		c_x = (x + s_x)*CELL_WIDTH;
	else {
		c_x = (scr_x - 1)*CELL_WIDTH;
		s_x = x + (scr_x - 1);
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
		c_y = y + s_y;
	else {
		c_y = scr_y - 1;
		s_y = y + (scr_y - 1);
	}
}

void delete_row() {
	if (num_rows != 1) {
		for (int i = 0; i < num_cols; i++)
			free(matrix[y][i]);
		free(matrix[y]);

		for (int i = y; i < num_rows - 1; i++)
			matrix[i] = matrix[i + 1];

		matrix[num_rows - 1] = NULL;
		num_rows--;
		if (y == num_rows) y--;
	}
}

void delete_col() {
	if (num_cols != 1) {
		for (int j = 0; j < num_rows; j++) {
			free(matrix[j][x]);
			for (int i = x; i < num_cols - 1; i++)
				matrix[j][i] = matrix[j][i + 1];
			matrix[j][num_cols - 1] = NULL;
		}
		num_cols--;
		if (x == num_cols) x--;
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
			mvprintw(c_y, c_x, "%*s", CELL_WIDTH, ""); // clear cell
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
					memmove(buffer + i, buffer + i + 1, strlen(buffer) - i + 1);
				}
				str_size--;
				memmove(buffer + i, buffer + i + 1, strlen(buffer) - i + 1);
            }
        }
        else if (key == KEY_DC) {
			if (i <= str_size) {
				str_size--;
				memmove(buffer + i, buffer + i + 1, strlen(buffer) - i + 1);
				while ((buffer[i] & 0xC0) == 0x80) {
					str_size--;
					memmove(buffer + i, buffer + i + 1, strlen(buffer) - i + 1);
				}
            }
        }
		else if (key == KEY_RESIZE) {
			when_resize();
		}
		else {
			memmove(buffer + i + 1, buffer + i, strlen(buffer) - i + 1);
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

void visual_start() {
	if (mode != 'v') {
		mode = 'v';
		c_y0 = c_y;
		c_x0 = c_x;
		s_y0 = s_y;
		s_x0 = s_x;
		v_y = y;
		v_x = x;
		ch[0] = y;
		ch[1] = y + 1;
		ch[2] = x;
		ch[3] = x + 1;
	}
	else {
		mode = 'n';
		ch[0], ch[1], ch[2], ch[3] = 0;
	}
}
 
void visual_end() {
	mode = 'n';
	ch[0], ch[1], ch[2], ch[3] = 0;
	y = v_y;
	x = v_x;
	c_y = c_y0;
	c_x = c_x0;
	s_y = s_y0;
	s_x = s_x0;
}

void visual() {
	int key;
	mode = 'n';
	key = getch();
	if (key == 'l' || key == 'h') {
		visual_start();
		ch[0] = y;
		ch[1] = y + 1;
		ch[2] = 0;
		ch[3] = num_cols + 1;
	}
	else if (key == 'j' || key == 'k') {
		visual_start();
		ch[0] = 0;
		ch[1] = num_rows + 1;
		ch[2] = x;
		ch[3] = x + 1;
	}
}

void wipe_cells() {
	for (int i=ch[0]; i<ch[1]; i++) {
		for (int j=ch[2]; j<ch[3]; j++) {
			free(matrix[i][j]);
			matrix[i][j] = strdup("");
		}
	}
	visual_end();
}

void deleting() {
	int key;
	key = getch();
	if (key == 'l')
		delete_row();
	else if (key == 'j')
		delete_col();
}

void str_change() {
	if (mode == 'v') visual_end();
    char* temp = get_str("", 0);
	free(matrix[y][x]);
    matrix[y][x] = strdup(temp);
    free(temp);
}

void str_append() {
	if (mode == 'v') visual_end();
	char* temp = get_str(matrix[y][x], 1);
	free(matrix[y][x]);
    matrix[y][x] = strdup(temp);
    free(temp);
}

void str_insert() {
	if (mode == 'v') visual_end();
	char* temp = get_str(matrix[y][x], 0);
	free(matrix[y][x]);
    matrix[y][x] = strdup(temp);
    free(temp);
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

static Key keys[] = {
	{'q', quit, {0}},
	{'v', visual_start, {0}},
	{'V', visual, {0}},
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
	{'s', write_csv, { .filename = NULL} }, // filename will be set at runtime
	{'d', wipe_cells, {0}},
	{'D', deleting, {0}}
};

void keypress(int key, Arg targ) {
	for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		if (key == keys[i].key) {
			if (keys[i].func == write_csv) (*keys[i].func)(&targ);
			else (*keys[i].func)(&keys[i].arg);
		}
	}
}

char **split_string(const char* str, const char delimiter, int* num_tokens) {
    int count = 1;
    const char *tmp = str;
    char **result = NULL;
    char *token;
    char delim[2];
    delim[0] = delimiter;
    delim[1] = '\0';

    // Preštej število elementov
    while (*tmp) {
        if (*tmp == delimiter) {
            count++;
        }
        tmp++;
    }

    // Dodeli pomnilnik za shranjevanje podnizov
    result = (char **) malloc((count + 2) * sizeof(char *));
    if (!result) {
        perror("Napaka pri dodeljevanju pomnilnika");
        return NULL;
    }

    int i = 0;
    tmp = str;
    while (*tmp) {
        const char *start = tmp;
        while (*tmp && *tmp != delimiter) {
            tmp++;
        }

        // Dodeli in kopiraj podniz
        if (start == tmp) {
            result[i] = strdup("");
        } else {
            result[i] = (char *) malloc((tmp - start + 1) * sizeof(char));
            if (!result[i]) {
                perror("Napaka pri dodeljevanju pomnilnika");
                for (int j = 0; j < i; j++) {
                    free(result[j]);
                }
                free(result);
                return NULL;
            }
            strncpy(result[i], start, tmp - start);
            result[i][tmp - start] = '\0';
        }
        i++;
        if (*tmp) {
            tmp++;
        }
    }
	if (*(tmp - 1) == delimiter) {
        result[i] = strdup("");
        i++;
    }
    result[i] = NULL;
    *num_tokens = i;

    return result;
}

char ***read_to_matrix(FILE *file, int *num_rows, int *num_cols) {
	int buff_rows, buff_cols = 20;
	char ***matrix = (char ***) malloc(buff_rows*sizeof(char **));
    if (!matrix) {
        perror("Napaka pri dodeljevanju pomnilnika za vrstice");
        return NULL;
    }
	char *line_buf = NULL;
	size_t line_buf_size = 0;
	ssize_t line_size;
	*num_rows = 0;
	*num_cols = 0;

	if ((line_size = getline(&line_buf, &line_buf_size, file)) == -1) {
		matrix[*num_rows] = (char **) malloc(buff_cols * sizeof(char *));
		 if (!matrix[0]) {
            perror("Napaka pri dodeljevanju pomnilnika za stolpce");
            free(matrix);
            return NULL;
        }
		matrix[*num_rows][*num_cols] = strdup("");
		(*num_cols)++;
		(*num_rows)++;
	}
	while (line_size >= 0) {
        if (*num_rows >= buff_rows - 2) { // If buffer is nearly full, increase its size
            buff_rows += 50;
            char*** new_matrix = (char***)realloc(matrix, buff_rows * sizeof(char**));
            if (new_matrix == NULL) {
                free(matrix);
                fprintf(stderr, "Memory reallocation failed\n");
                exit(1);
            }
            matrix = new_matrix;
        }
		line_buf[strcspn(line_buf, "\n")] = '\0';

		matrix[*num_rows] = split_string(line_buf, ',', num_cols);

		(*num_rows)++;
		line_size = getline(&line_buf, &line_buf_size, file);
	}

	free(line_buf);
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
	set_tabsize(CELL_WIDTH);
    keypad(stdscr, TRUE); // omogoči uporabo funkcij, kot so KEY_LEFT
	int key;
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


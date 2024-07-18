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

#define CELL_WIDTH 10
#define FIFO "/tmp/pyfifo"
#define XCLIP_COPY "vis-clipboard --copy"
#define XCLIP_PASTE "vis-clipboard --paste"
#define MOVE_X 3
#define MOVE_Y 5

/* enums */
enum {PipeTo, PipeThrough, PipeRead, PipeAwk, PipeToClip, PipeReadClip};

char ***matrix;
char ***mat_reg = NULL;
int reg_rows, reg_cols = 0;
int num_rows, num_cols;
int rows, cols;
int y, x = 0;
int c_y, c_x = 0;
int v_y, v_x = 0;
int s_y, s_x = 0;
int s_y0, s_x0 = 0;
int ch[4] = {0, 0, 0, 0};
char mode = 'n';
int scr_x, scr_y;
char *fname;
int to_num_y;
int to_num_x;

typedef union {
	int i;
} Arg;

typedef struct {
	int key;
	void (*func)(const Arg *arg);
	const Arg arg;
} Key;

typedef struct node {
	char operation;
    char *** mat;
    char * cell;
	int y;
	int x;
	int s_y;
	int s_x;
	int rows;
	int cols;
	int add_y;
	int add_x;
	struct node * next;
	struct node * prev;
} node_t;
node_t * head = NULL;

size_t utf8_strlen(const char *str);
void draw();
void move_y(const Arg *arg);
void move_up(const Arg *arg);
void move_x(const Arg *arg);
void when_resize();
void insert_col(const Arg *arg);
void insert_row(const Arg *arg);
void delete_row();
void delete_col();
char* get_str(char* str, char loc, const char cmd);
void visual_start();
void visual_end();
void visual();
char** split_string(const char* str, const char delimiter, int* num_tokens, char keep_last);
void write_csv(const Arg *arg);
void write_to_pipe(const Arg *arg);
void yank_cells();
void wipe_cells();
void undo();
void redo();
void paste_cells();
void deleting();
void str_change();
void quit();
void keypress(int key);
char ***read_to_matrix(FILE *file, int *num_rows, int *num_cols);
void push(node_t ** head, char operation, char *** mat, char * cell, int rows, int cols, int y, int x, int s_y, int s_x, int add_y, int add_x);
void move_n();
void write_selection(int fd);
char **parse_command(char * cmd, const int arg);

static Key keys[] = {
	{'q', quit, {0}},
	{'v', visual_start, {0}},
	{'V', visual, {0}},
	{'\x03', visual_end, {0}}, //Ctrl-C
	{'j', move_y, {.i = 1}},
	{KEY_DOWN, move_y, {.i = 1}},
	{'k', move_y, {.i = -1}},
	{KEY_UP, move_y, {.i = -1}},
	{'l', move_x, {.i = 1}},
	{KEY_RIGHT, move_x, {.i = 1}},
	{'h', move_x, {.i = -1}},
	{KEY_LEFT, move_x, {.i = -1}},
	{'\x04', move_y, {.i = MOVE_Y}}, //Ctrl-D
	{'\x15', move_y, {.i = -MOVE_Y}}, //Ctrl-U
	{'w', move_x, {.i = MOVE_X}},
	{'b', move_x, {.i = -MOVE_X}},
	{'G', move_y, {.i = 100}},
	{'g', move_y, {.i = 0}},
	{'$', move_x, {.i = 100}},
	{'0', move_x, {.i = 0}},
	{'c', str_change, {0}},
	{'a', str_change, {2}},
	{'i', str_change, {1}},
	{'O', insert_row, {0}},
	{'o', insert_row, {1}},
	{'I', insert_col, {0}},
	{'A', insert_col, {1}},
	{'s', write_csv, {0}},
	{'S', write_csv, {1}},
	{'e', write_csv, {2}},
	{'E', write_csv, {3}},
	{'\x13', write_csv, {4}}, //Ctrl-S
	{'>', write_to_pipe, {PipeTo}},
	{'|', write_to_pipe, {PipeThrough}},
	{'\x0F', write_to_pipe, {PipeAwk}}, //Ctrl-O awk
	{'<', write_to_pipe, {PipeRead}},
	{'d', wipe_cells, {0}},
	{'y', yank_cells, {0}},
	{'Y', write_to_pipe, {PipeToClip}},
	{'p', paste_cells, {0}},
	{'P', write_to_pipe, {PipeReadClip}},
	{'u', undo, {0}},
	{'\x12', redo, {0}}, //Ctrl-R
	{':', move_n, {0}}, //Ctrl-R
	{'D', deleting, {0}}
};

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

void draw() {
	clear();
	int draw_cols, draw_rows;
	if (scr_x < num_cols)
		draw_cols = scr_x;
	else
		draw_cols = num_cols;
	if (scr_y < num_rows)
		draw_rows = scr_y;
	else
		draw_rows = num_rows;
	for (int i = 0; i < draw_rows; i++) {
		for (int j = 0; j < draw_cols; j++) {
			if (ch[0] <= s_y + i && s_y + i < ch[1] && ch[2] <= s_x + j && s_x + j < ch[3]) {
				attron(A_STANDOUT);
			}
			else attroff(A_STANDOUT);
			char* cell_value = matrix[i + s_y][j + s_x];
			int utf8_w = 0;
			int k = 0;
			for (; k < CELL_WIDTH*4; k++) {
				if (utf8_w == CELL_WIDTH - 1 || cell_value[k] == '\0') {
					break;
				}
				if ((cell_value[k] & 0xC0) != 0xC0) utf8_w++;
			}
			k = CELL_WIDTH - 1 + k - utf8_w;
			mvprintw(i, j * CELL_WIDTH, "%-*.*s", k, k, cell_value);
		}
	}
	wmove(stdscr, c_y, c_x);
	attroff(A_STANDOUT);
}

void move_y(const Arg *arg) {
	int move;
	if (arg->i == 99)
		move = to_num_y;
	else if (arg->i == 100 || y + arg->i >= num_rows)
		move = num_rows - y - 1;
	else if (arg->i == 0 || y + arg->i < 0)
		move = -y;
	else
		move = arg->i;
	y += move;
	if (mode == 'v') {
		if (y >= v_y) {
			ch[0] = v_y;
			ch[1] = y + 1;
		}
		else {
			ch[0] = y;
			ch[1] = v_y + 1;
		}
	}
}

void move_x(const Arg *arg) {
	int move;
	if (arg->i == 99)
		move = to_num_x;
	else if (arg->i == 100 || x + arg->i >= num_cols)
		move = num_cols - x - 1;
	else if (arg->i == 0 || x + arg->i < 0)
		move = -x;
	else
		move = arg->i;
	x += move;
	if (mode == 'v') {
		if (x >= v_x) {
			ch[2] = v_x;
			ch[3] = x + 1;
		}
		else {
			ch[2] = x;
			ch[3] = v_x + 1;
		}
	}
}

void move_n() {
	char *temp = get_str("", 0, ':');
    int length = strlen(temp);
	to_num_y = 0;
	to_num_x = x;
    int is_number = 0;
	char next = 0;

    for (int i = 0; i < length; i++) {
        if (*(temp + i) >= '0' && *(temp + i) <= '9') {
			if (next == 0)
				to_num_y = to_num_y * 10 + (*(temp + i) - '0');
			else if (next == 1)
				to_num_x = to_num_x * 10 + (*(temp + i) - '0');
            is_number = 1;
        }
		else if (*(temp + i) == '.') {
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
	if (is_number) {
		to_num_y -= y;
		to_num_x -= x;
		Arg move;
		move.i = 99;
		if (to_num_y != 0 && y + to_num_y < num_rows && y + to_num_y >= 0)
			move_y(&move);
		if (to_num_x != 0 && x + to_num_x < num_cols && x + to_num_x >= 0)
			move_x(&move);
    }
}

void when_resize() {
	getmaxyx(stdscr, rows, cols);
	scr_x = cols/CELL_WIDTH;
	scr_y = rows;
	if (y <= s_y)
		s_y = y;
	if (y > s_y + scr_y - 1)
		s_y = y - (scr_y - 1);
	if (scr_y - (num_rows - s_y) > 0)
		s_y -= scr_y - (num_rows - s_y);
	if (s_y < 0)
		s_y = 0;
	if (x <= s_x)
		s_x = x;
	if (x > s_x + scr_x - 1)
		s_x = x - (scr_x - 1);
	if (scr_x - (num_cols - s_x) > 0)
		s_x -= scr_x - (num_cols - s_x);
	if (s_x < 0)
		s_x = 0;
	c_x = (x - s_x)*CELL_WIDTH;
	c_y = y - s_y;
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
	push(&head, 'h', NULL, NULL, arg->i, 1, y, x, s_y, s_x, 0, 0);
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
	push(&head, 'g', NULL, NULL, 1, arg->i, y, x, s_y, s_x, 0, 0);
}

void delete_row() {
	int num = ch[1] - ch[0];
	if (num_rows > num) {
		char ***undo_mat = (char ***)malloc(num*sizeof(char**));
		for (int i = 0; i < num; i++)
			undo_mat[i] = matrix[ch[0] + i];
		for (int i = ch[0]; i < num_rows - num; i++)
			matrix[i] = matrix[i + num];
		matrix = realloc(matrix, (num_rows - num)*sizeof(char**));
		num_rows -= num;
		push(&head, 'e', undo_mat, NULL, num, num_cols, ch[0], x, s_y, s_x, 0, 0);
		y = ch[0];
		if (y >= num_rows)
			y = ch[0] - 1;
		ch[0], ch[1], ch[2], ch[3] = 0;
		mode = 'n';
	}
}

void delete_col() {
	int num = ch[3] - ch[2];
	char ***undo_mat = (char ***)malloc(num_rows*sizeof(char **));
	if (num_cols > num) {
		for (int j = 0; j < num_rows; j++) {
			undo_mat[j] = (char **)malloc(num * sizeof(char *));
			for (int i = 0; i < num; i++)
				undo_mat[j][i] = matrix[j][ch[2] + i];
			for (int i = ch[2]; i < num_cols - num; i++)
				matrix[j][i] = matrix[j][i + num];
			matrix[j] = realloc(matrix[j], (num_cols - num)*sizeof(char *));
		}
		num_cols -= num;
		push(&head, 'f', undo_mat, NULL, num_rows, num, y, ch[2], s_y, s_x, 0, 0);
		x = ch[2];
		if (x >= num_cols)
			x = ch[2] - 1;
		ch[0], ch[1], ch[2], ch[3] = 0;
		mode = 'n';
	}
}

char* get_str(char* str, char loc, const char cmd) {
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
			when_resize();
			draw();
			if (cmd != 0) {
				c_x = 1;
				c_y = rows - 1;
				mvaddch(c_y, c_x-1, cmd);
			}
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
			if (cmd == 0) {
				mode = 'i';
			}
			break;
		}
		if (key == '\t') {
			if (cmd == 0) {
				mode = 'j';
			}
			break;
		}
		else if (key == '\x03' && cmd == 0) {
			mode = 'n';
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
				do {
					str_size--;
					memmove(buffer + i, buffer + i + 1, strlen(buffer) - i + 1);
				}
				while ((buffer[i] & 0xC0) == 0x80);
            }
        }
		else if (key == KEY_RESIZE);
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
	ch[0], ch[1], ch[2], ch[3] = 0;
	if (mode != 'n') {
		mode = 'n';
		y = v_y;
		x = v_x;
		s_y = s_y0;
		s_x = s_x0;
	}
}

void visual() {
	int key;
	key = getch();
	if (key == 'l' || key == 'h' || key == '$' || key == '0' ||
			key == 'w' || key == 'b' || key == KEY_RIGHT || key == KEY_LEFT) {
		mode = 'n';
		visual_start();
		v_y = 0;
		ch[0] = 0;
		ch[1] = num_rows;
		ch[2] = x;
		ch[3] = x + 1;
		if (key == '$') {
			Arg move;
			move.i = 100;
			move_x(&move);
		}
		if (key == '0') {
			Arg move;
			move.i = 0;
			move_x(&move);
		}
		if (key == 'w') {
			Arg move;
			move.i = MOVE_X;
			move_x(&move);
		}
		if (key == 'b') {
			Arg move;
			move.i = -MOVE_X;
			move_x(&move);
		}
	}
	else if (key == 'j' || key == 'k' || key == 'G' || key == 'g' ||
			key == '\x04' || key == '\x15' || key == KEY_UP || key == KEY_DOWN) {
		mode = 'n';
		visual_start();
		v_x = 0;
		ch[0] = y;
		ch[1] = y + 1;
		ch[2] = 0;
		ch[3] = num_cols;
		if (key == 'G') {
			Arg move;
			move.i = 100;
			move_y(&move);
		}
		if (key == 'g') {
			Arg move;
			move.i = 0;
			move_y(&move);
		}
		if (key == '\x04') {
			Arg move;
			move.i = MOVE_Y;
			move_y(&move);
		}
		if (key == '\x15') {
			Arg move;
			move.i = -MOVE_Y;
			move_y(&move);
		}
	}
}

char** split_string(const char* str, const char delimiter, int* num_tokens, char keep_last) {
    int count = 1;
    const char *tmp = str;
    char **result = NULL;
    char *token;
    char delim[2];
    delim[0] = delimiter;
    delim[1] = '\0';

    while (*tmp) { // count for malloc
        if (*tmp == delimiter) {
            count++;
        }
        tmp++;
    }

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

        if (start == tmp) { // if delimiter at the beginning
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
	if (keep_last == 1) {
		if (*(tmp - 1) == delimiter) { // if delimiter at the end
			result[i] = strdup("");
			i++;
		}
	}
    result[i] = NULL;
    *num_tokens = i;

    return result;
}

void write_csv(const Arg *arg) {
	char flip;
	char *filename;

	if (arg->i < 2)
		filename = get_str("", 0, ':');
	else if (arg->i == 4) {
		if (fname == NULL)
			filename = get_str("", 0, ':');
		else {
			filename = (char *)malloc(strlen(fname) + 1);
			strcpy(filename, fname);
		}
	}
	else {
		filename = (char *)malloc(strlen(FIFO) + 1);
		strcpy(filename, FIFO);
		int fd = open(filename, O_WRONLY | O_NONBLOCK);
		if (fd == -1) {
			if (errno == ENXIO) {
				mvaddstr(rows-1, 0, " Nobody listens. ");
				getch();
			} else {
				perror("open");
			}
			close(fd);
			return;
		}
		close(fd);
	}
	if (strlen(filename) == 0) {
		addstr(" Empty filename. ");
		getch();
	}
	else {
		FILE *file = fopen(filename, "w");
		if (!file) {
			perror("Error opening file for writing");
		}

		if (mode == 'n') {
			ch[0] = 0;
			ch[1] = num_rows;
			ch[2] = 0;
			ch[3] = num_cols;
		}

		if (arg->i == 1 || arg->i == 2) 
				flip = 1;

		if (flip == 1) {
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
		if (arg->i == 2 || arg->i == 3) {
			first = "=[";
			end = "]";
		}
		for (int i = ch[0]; i < ch[1]; i++) {
			for (int j = ch[2]; j < ch[3]; j++) {
				if (flip == 1)
					fprintf(file, "%s", matrix[j][i]);
				else
					fprintf(file, "%s", matrix[i][j]);
				if (j == ch[3]-1) {
					if (end != "" && j != ch[2])
						fprintf(file, end);
					fprintf(file, "\n");
				}
				else if (j == ch[2] && first != "")
					fprintf(file, first);
				else
					fprintf(file, ",");
			}
		}
		fclose(file);
	}

	free(filename);
	visual_end();
}

char **parse_command(char * cmd, int arg) {
	char* temp = strdup(cmd);
	int num_args = 0;
	char* token = strtok(temp, " ");
	while (token != NULL) {
		num_args++;
		token = strtok(NULL, " ");
	}
	free(temp);
	char** cmd_arg = malloc((num_args+1)*sizeof(char*));
	if (cmd_arg == NULL)
		return NULL;
	if (arg == PipeAwk) {
		cmd_arg[0] = "awk";
		cmd_arg[1] = "-F,";
		cmd_arg[2] = "-vOFS=,";
		cmd_arg[3] = cmd;
		cmd_arg[4] = NULL;
	}
	else {
		int i = 0;
		token = strtok(cmd, " ");
		while (token != NULL) {
			cmd_arg[i++] = token;
			token = strtok(NULL, " ");
		}
		cmd_arg[i] = NULL;
	}

	return cmd_arg;
}

void write_selection(int fd) {
	char flip = 0;
	for (int i = ch[0]; i < ch[1]; i++) {
		for (int j = ch[2]; j < ch[3]; j++) {
			if (flip == 1)
				write(fd, matrix[j][i], strlen(matrix[j][i]));
			else
				write(fd, matrix[i][j], strlen(matrix[i][j]));
			if (j == ch[3]-1)
				write(fd, "\n", 1);
			else
				write(fd, ",", 1);
		}
	}
}

void write_to_pipe(const Arg *arg) {
	char* cmd;
	if (arg->i == PipeThrough)
		cmd = get_str("", 0, '|');
	else if (arg->i == PipeTo)
		cmd = get_str("", 0, '>');
	else if (arg->i == PipeRead)
		cmd = get_str("", 0, '<');
	else if (arg->i == PipeAwk)
		cmd = get_str("", 0, '|');
	else if (arg->i == PipeToClip) {
		cmd = malloc(30);
		strcpy(cmd, XCLIP_COPY);
	}
	else if (arg->i == PipeReadClip) {
		cmd = malloc(30);
		strcpy(cmd, XCLIP_PASTE);
	}

    int pipefd[2];
    int pipefd2[2];
	pid_t pid;
	int status;
    ssize_t bytes_read;
	char* buffer;
	size_t buffer_size = 20;
	size_t total_bytes = 0;
	
	if (arg->i != PipeRead && arg->i != PipeReadClip) {
		//create pipe
		if (pipe(pipefd) == -1) {
			perror("pipe");
			exit(1);
		}
	}

    if (pipe(pipefd2) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

	// fork process
    pid = fork();
    if (pid == -1) {
        perror("fork");
        exit(2);
    }

    if (pid == 0) { // Child: Connect pipA[0] (read) to standard input 0
		if (arg->i != PipeRead && arg->i != PipeReadClip) {
			// redirect stdin to input
			close(pipefd[1]);
			dup2(pipefd[0], STDIN_FILENO);
			close(pipefd[0]);
		}

		// redirect stdout to output
        close(pipefd2[0]);
        dup2(pipefd2[1], STDOUT_FILENO);
        close(pipefd2[1]);

		char **cmd_arg = parse_command(cmd, arg->i);

        execvp(cmd_arg[0], cmd_arg);
        // if execlp witout success
        perror(" execvp");
        exit(EXIT_FAILURE);
		free(cmd);
		free(cmd_arg);
	}
	else {  // Parent process
        close(pipefd2[1]);

		if (arg->i != PipeRead && arg->i != PipeReadClip) {
			close(pipefd[0]);
			if (mode == 'n') {
				ch[0] = 0;
				ch[1] = num_rows;
				ch[2] = 0;
				ch[3] = num_cols;
			}

			write_selection(pipefd[1]);
			close(pipefd[1]);
		}

        buffer = (char *)malloc(buffer_size);
        if (buffer == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

		while ((bytes_read = read(pipefd2[0], buffer + total_bytes, buffer_size - total_bytes - 1)) > 0) {
            total_bytes += bytes_read;
            if (total_bytes >= buffer_size - 1) {
                buffer_size *= 2;
                buffer = (char *)realloc(buffer, buffer_size);
                if (buffer == NULL) {
                    perror("realloc");
                    exit(EXIT_FAILURE);
                }
            }
        }

        close(pipefd2[0]);  // close output

		waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			getch();
        }

		if (total_bytes > 0) {
			buffer[total_bytes] = '\0';

			if (arg->i == '>') {
				visual_end();
				clear();
				mvprintw(0, 0, buffer);
				getch();
			}
			else {
				if (arg->i != PipeRead && arg->i != PipeReadClip)
					visual_end();
				int num_cols_2, num_rows_2;
				char** temp = split_string(buffer, '\n', &num_rows_2, 0);
				int add_y, add_x = 0;
				if (y + num_rows_2 - num_rows > 0)
					add_y = y + num_rows_2 - num_rows;
				char *** undo_mat = (char***)malloc(num_rows_2 * sizeof(char**));
				char *** paste_mat = (char***)malloc(num_rows_2 * sizeof(char**));
				/* If not enough rows */
				if (num_rows_2 - (num_rows - y) > 0) {
					matrix = (char ***)realloc(matrix, (y + num_rows_2)*sizeof(char **));
					for (int i = num_rows; i < y + num_rows_2; i++) {
						matrix[i] = (char **)malloc(num_cols * sizeof(char *));
						for (int j = 0; j < num_cols; j++) {
							matrix[i][j] = strdup("");
						}
					}
					num_rows = y + num_rows_2;
				}
				for (int i = 0; i < num_rows_2; i++) {
					char** temp2 = split_string(temp[i], ',', &num_cols_2, 1);
					if (i == 0) {
						if (x + num_cols_2 - num_cols > 0)
							add_x = x + num_cols_2 - num_cols;
						/* If not enough cols */
						if (num_cols_2 - (num_cols - x) > 0) {
							for (int i = 0; i < num_rows; i++) {
								matrix[i] = (char **)realloc(matrix[i], (num_cols + add_x)*sizeof(char *));
								for (int j = num_cols; j < num_cols + add_x; j++) {
									matrix[i][j] = strdup("");
								}
							}
							num_cols = x + num_cols_2;
						}
					}
					undo_mat[i] = (char**)malloc(num_cols_2 * sizeof(char*));
					paste_mat[i] = (char**)malloc(num_cols_2 * sizeof(char*));
					for (int j = 0; j < num_cols_2; j++) {
						undo_mat[i][j] = matrix[y + i][x + j];
						paste_mat[i][j] = strdup(temp2[j]);
						matrix[y + i][x + j] = strdup(temp2[j]);
					}
					free(temp2);
				}
				push(&head, 'p', undo_mat, NULL, num_rows_2, num_cols_2, y, x, s_y, s_x, add_y, add_x);
				push(&head, 'p', paste_mat, NULL, num_rows_2, num_cols_2, y, x, s_y, s_x, 0, 0);
				free(temp);
			}
			free(buffer);
		}
		else if (arg->i == PipeToClip)
			visual_end();
    }
}

void yank_cells() {
	for (int i = 0; i < reg_rows; i++) {
		for (int j = 0; j < reg_cols && mat_reg[i][j] != NULL; j++) {
			free(mat_reg[i][j]);
		}
		free(mat_reg[i]);
	}
	free(mat_reg);
	reg_rows = (ch[1]-ch[0]);
	reg_cols = (ch[3]-ch[2]);
	mat_reg = (char***)malloc(reg_rows * sizeof(char**));
	for (int i=0; i<reg_rows; i++)
		mat_reg[i] = (char**)malloc(reg_cols * sizeof(char*));
	for (int i=ch[0]; i<ch[1]; i++) {
		for (int j=ch[2]; j<ch[3]; j++) {
			mat_reg[i-ch[0]][j-ch[2]] = strdup(matrix[i][j]);
		}
	}
	visual_end();
}

void wipe_cells() {
	if (mode == 'v') {
		int range_rows = (ch[1]-ch[0]);
		int range_cols = (ch[3]-ch[2]);

		for (int i = 0; i < reg_rows; i++) {
			for (int j = 0; j < reg_cols && mat_reg[i][j] != NULL; j++) {
				free(mat_reg[i][j]);
			}
			free(mat_reg[i]);
		}
		free(mat_reg);
		reg_rows = range_rows;
		reg_cols = range_cols;
		mat_reg = (char***)malloc(reg_rows * sizeof(char**));
		for (int i=0; i<reg_rows; i++) {
			mat_reg[i] = (char**)malloc(reg_cols * sizeof(char*));
		}
		char *** undo_mat = (char***)malloc(range_rows * sizeof(char**));
		for (int i=0; i<range_rows; i++) {
			undo_mat[i] = (char**)malloc(range_cols * sizeof(char*));
		}
		for (int i=ch[0]; i<ch[1]; i++) {
			for (int j=ch[2]; j<ch[3]; j++) {
				undo_mat[i-ch[0]][j-ch[2]] = matrix[i][j];
				mat_reg[i-ch[0]][j-ch[2]] = strdup(matrix[i][j]);
				matrix[i][j] = strdup("");
			}
		}
		push(&head, 'd', undo_mat, NULL, reg_rows, reg_cols, ch[0], ch[2], s_y, s_x, 0, 0);

		visual_end();
	}
}

void push(node_t ** head, char operation,  char *** mat, char * cell, int rows, int cols, int y, int x, int s_y, int s_x, int add_y, int add_x) {
	if (*head == NULL) {
		*head = (node_t *) malloc(sizeof(node_t));
		(*head)->next = NULL;
		(*head)->prev = NULL;
	}
    node_t * new_node;
    new_node = (node_t *) malloc(sizeof(node_t));
	if (new_node == NULL) {
		exit(1);
	}

	new_node->operation = operation;
	new_node->mat = mat;
	new_node->cell = cell;
	new_node->rows = rows;
	new_node->cols = cols;
	new_node->y = y;
	new_node->x = x;
	new_node->s_y = s_y;
	new_node->s_x = s_x;
	new_node->add_y = add_y;
	new_node->add_x = add_x;
	new_node->next = *head;
	new_node->prev = NULL;

	// free previous nodes if in the middle of history
	while ((*head)->prev != NULL) {
		node_t * temp = (*head)->prev;
		(*head)->prev = (*head)->prev->prev;
		free(temp->mat);
		free(temp);
	}

	(*head)->prev = new_node;
	*head = new_node;
}

void undo() {
	if (head != NULL && head->next != NULL) {
		if (head->operation == 'd') {
			for (int i = 0; i < head->rows; i++) {
				for (int j = 0; j < head->cols; j++) {
					free(matrix[head->y + i][head->x + j]);
					matrix[head->y + i][head->x + j] = strdup(head->mat[i][j]);
				}
			}
		}
		else if (head->operation == 'i') {
			for (int i=head->y; i<(head->y + head->rows); i++) {
				for (int j=head->x; j<(head->x + head->cols); j++) {
					free(matrix[i][j]);
					matrix[i][j] = strdup("");
				}
			}
		}
		else if (head->operation == 'p') {
			head = head->next;
			for (int i = 0; i < head->rows; i++) {
				for (int j = 0; j < head->cols; j++) {
					free(matrix[head->y + i][head->x + j]);
					matrix[head->y + i][head->x + j] = strdup(head->mat[i][j]);
				}
			}
			for (int j = 0; j < num_rows; j++) {
				if (j < num_rows - head->add_y) {
					for (int i = 1; i <= head->add_x; i++)
						free(matrix[j][num_cols - i]);
					matrix[j] = (char **)realloc(matrix[j], (num_cols - head->add_x)*sizeof(char*));
				}
				else {
					for (int i = 0; i < num_cols; i++)
						free(matrix[j][i]);
					free(matrix[j]);
				}
			}
			matrix = (char ***)realloc(matrix, (num_rows - head->add_y)*sizeof(char**));
			num_cols -= head->add_x;
			num_rows -= head->add_y;
		}
		else if (head->operation == 'r') {
			free(matrix[head->y][head->x]);
			matrix[head->y][head->x] = strdup("");
			head = head->next;
			free(matrix[head->y][head->x]);
			matrix[head->y][head->x] = strdup(head->cell);
		}
		else if (head->operation == 'e') {
			matrix = (char ***)realloc(matrix, (num_rows + head->rows) * sizeof(char **));
			for (int i = num_rows + head->rows - 1; i >= head->y + head->rows; i--) {
				matrix[i] = matrix[i - head->rows];
			}
			for (int i = 0; i < head->rows; i++) {
				matrix[head->y + i] = (char **)malloc(num_cols * sizeof(char *));
				for (int j = 0; j < num_cols; j++) {
					matrix[head->y + i][j] = strdup(head->mat[i][j]);
				}
			}
			num_rows += head->rows;
		}
		else if (head->operation == 'f') {
			for (int i = 0; i < num_rows; i++) {
				matrix[i] = (char **)realloc(matrix[i], (num_cols + head->cols) * sizeof(char *));
				for (int j = num_cols + head->cols - 1; j >= head->x + head->cols; j--) {
					matrix[i][j] = matrix[i][j - head->cols];
				}
				for (int j = 0; j < head->cols; j++)
					matrix[i][head->x + j] = strdup(head->mat[i][j]);
			}
			num_cols += head->cols;
		}
		else if (head->operation == 'g') {
			for (int i = 0; i < num_cols; i++)
				free(matrix[head->y + head->cols][i]);
			free(matrix[head->y + head->cols]);

			for (int i = head->y + head->cols; i < num_rows - 1; i++)
				matrix[i] = matrix[i + 1];

			matrix[num_rows - 1] = NULL;
			num_rows--;
		}
		else if (head->operation == 'h') {
			for (int j = 0; j < num_rows; j++) {
				free(matrix[j][head->x + head->rows]);
				for (int i = head->x + head->rows; i < num_cols - 1; i++)
					matrix[j][i] = matrix[j][i + 1];
				matrix[j][num_cols - 1] = NULL;
			}
			num_cols--;
		}
		else if (head->operation == 's') {
			for (int i = 0; i < num_cols; i++)
				free(matrix[head->y][i]);
			free(matrix[head->y]);

			matrix[num_rows - 1] = NULL;
			num_rows--;
		}
		else if (head->operation == 't') {
			for (int j = 0; j < num_rows; j++) {
				free(matrix[j][head->x]);
				matrix[j][num_cols - 1] = NULL;
			}
			num_cols--;
		}
		if (head->y == num_rows)
			y = head->y - 1;
		else
			y = head->y;
		if (head->x == num_cols)
			x = head->x - 1;
		else
			x = head->x;
		s_y = head->s_y;
		s_x = head->s_x;
		head = head->next;
	}
}

void redo() {
	if (head != NULL && head->prev != NULL) {
		head = head->prev;
		if (head->operation == 'd') {
			for (int i=head->y; i<(head->y + head->rows); i++) {
				for (int j=head->x; j<(head->x + head->cols); j++) {
					free(matrix[i][j]);
					matrix[i][j] = strdup("");
				}
			}
		}
		else if (head->operation == 'i') {
			for (int i = 0; i < head->rows; i++) {
				for (int j = 0; j < head->cols; j++) {
					free(matrix[head->y + i][head->x + j]);
					matrix[head->y + i][head->x + j] = strdup(head->mat[i][j]);
				}
			}
		}
		else if (head->operation == 'p') {
			matrix = (char ***)realloc(matrix, (num_rows + head->add_y)*sizeof(char **));
			for (int i = 0; i < num_rows + head->add_y; i++) {
				if (i < num_rows) {
					matrix[i] = (char **)realloc(matrix[i], (num_cols + head->add_x)*sizeof(char *));
					for (int j = 0; j < head->add_x; j++)
						matrix[i][num_cols + j] = strdup("");
				}
				else {
					matrix[i] = (char **)malloc((num_cols + head->add_x)*sizeof(char *));
					for (int j = 0; j < num_cols + head->add_x; j++) {
						matrix[i][j] = strdup("");
					}
				}
			}
			num_rows += head->add_y;
			num_cols += head->add_x;
			if (head->prev != NULL)
				head = head->prev;
			for (int i = 0; i < head->rows; i++) {
				for (int j = 0; j < head->cols; j++) {
					free(matrix[head->y + i][head->x + j]);
					matrix[head->y + i][head->x + j] = strdup(head->mat[i][j]);
				}
			}
		}
		else if (head->operation == 'r') {
			free(matrix[head->y][head->x]);
			matrix[head->y][head->x] = strdup("");
			if (head->prev != NULL)
				head = head->prev;
			free(matrix[head->y][head->x]);
			matrix[head->y][head->x] = strdup(head->cell);
		}
		else if (head->operation == 'e') {
			int num = head->rows;
			for (int j = 0; j < num; j++) {
				for (int i = 0; i < num_cols; i++)
					free(matrix[head->y + j][i]);
				free(matrix[head->y + j]);
			}
			for (int i = head->y; i < num_rows - num; i++)
				matrix[i] = matrix[i + num];
			matrix = realloc(matrix, (num_rows - num)*sizeof(char**));
			num_rows -= num;
		}
		else if (head->operation == 'f') {
			int num = head->cols;
			for (int j = 0; j < num_rows; j++) {
				for (int i = 0; i < num; i++)
					free(matrix[j][head->x + i]);
				for (int i = head->x; i < num_cols - num; i++)
					matrix[j][i] = matrix[j][i + num];
				matrix[j] = realloc(matrix[j], (num_cols - num)*sizeof(char *));
			}
			num_cols -= num;
		}
		else if (head->operation == 'g') {
			for (int i = num_rows; i > head->y + head->cols; i--) {
				matrix[i] = matrix[i - 1];
			}
			num_rows++;
			matrix[head->y + head->cols] = (char **)malloc(num_cols * sizeof(char *));
			for (int j = 0; j < num_cols; j++) {
				matrix[head->y + head->cols][j] = strdup("");
			}
		}
		else if (head->operation == 'h') {
			for (int i = 0; i < num_rows; i++) {
				matrix[i] = (char **)realloc(matrix[i], (num_cols+1) * sizeof(char *));
				for (int j = num_cols; j > head->x + head->rows; j--) {
					matrix[i][j] = matrix[i][j - 1];
				}
				matrix[i][head->x + head->rows] = strdup("");
			}
			num_cols++;
		}
		else if (head->operation == 's') {
			num_rows++;
			matrix[head->y] = (char **)malloc(num_cols * sizeof(char *));
			for (int j = 0; j < num_cols; j++) {
				if (j == head->x)
					matrix[head->y][j] = strdup(head->cell);
				else
					matrix[head->y][j] = strdup("");
			}
		}
		else if (head->operation == 't') {
			num_cols++;
			for (int i = 0; i < num_rows; i++) {
				matrix[i] = (char **)realloc(matrix[i], num_cols * sizeof(char *));
				if (i == head->y)
					matrix[i][head->x] = strdup(head->cell);
				else
					matrix[i][head->x] = strdup("");
			}
		}
		if (head->y == num_rows)
			y = head->y - 1;
		else
			y = head->y;
		if (head->x == num_cols)
			x = head->x - 1;
		else
			x = head->x;
		s_y = head->s_y;
		s_x = head->s_x;
	}
}

void paste_cells() {
	if (mat_reg != NULL) {
		char *** undo_mat = (char***)malloc(reg_rows * sizeof(char**));
		char *** paste_mat = (char***)malloc(reg_rows * sizeof(char**));
		for (int i=0; i<reg_rows; i++) {
			undo_mat[i] = (char**)malloc(reg_cols * sizeof(char*));
			paste_mat[i] = (char**)malloc(reg_cols * sizeof(char*));
		}
		if (reg_rows <= (num_rows - y) && reg_cols <= (num_cols - x)) {
			for (int i = 0; i < reg_rows; i++) {
				for (int j = 0; j < reg_cols; j++) {
					undo_mat[i][j] = matrix[y + i][x + j];
					paste_mat[i][j] = strdup(mat_reg[i][j]);
					matrix[y + i][x + j] = strdup(mat_reg[i][j]);
				}
			}
			push(&head, 'p', undo_mat, NULL, reg_rows, reg_cols, y, x, s_y, s_x, 0, 0);
			push(&head, 'p', paste_mat, NULL, reg_rows, reg_cols, y, x, s_y, s_x, 0, 0);
		}
	}
}

void deleting() {
	if (mode == 'v') {
		if (ch[0] == 0 && ch[1] == num_rows)
			delete_col();
		else if (ch[2] == 0 && ch[3] == num_cols)
			delete_row();
	}
	else {
		int key;
		key = getch();
		if (key == 'l' || key == 'h') {
			ch[2] = x;
			ch[3] = x + 1;
			delete_col();
		}
		else if (key == 'j' || key == 'k') {
			ch[0] = y;
			ch[1] = y + 1;
			delete_row();
		}
	}
}

void str_change(const Arg *arg) {
	if (mode == 'v') visual_end();
	mode = 'i';
	char* temp;
	while (mode == 'i' || mode == 'j') {
		if (y < num_rows && x < num_cols) {
			if (arg->i == 0)
				temp = get_str("", 0, 0);
			else if (arg->i == 1)
				temp = get_str(matrix[y][x], 0, 0);
			else if (arg->i == 2)
				temp = get_str(matrix[y][x], 1, 0);
			char * undo_cell = matrix[y][x];
			char * paste_cell = strdup(temp);
			matrix[y][x] = strdup(temp);
			free(temp);

			push(&head, 'r', NULL, undo_cell, 0, 0, y, x, s_y, s_x, 0, 0);
			push(&head, 'r', NULL, paste_cell, 0, 0, y, x, s_y, s_x, 0, 0);
		}
		else if (y == num_rows) {
			num_rows++;
			matrix[y] = (char **)malloc(num_cols * sizeof(char *));
			for (int j = 0; j < num_cols; j++) {
				matrix[y][j] = strdup("");
			}
			temp = get_str("", 0, 0);
			matrix[y][x] = strdup(temp);
			push(&head, 's', NULL, strdup(temp), 0, 0, y, x, s_y, s_x, 0, 0);
			free(temp);
		}
		else if (x == num_cols) {
			temp = get_str("", 0, 0);
			num_cols++;
			for (int i = 0; i < num_rows; i++) {
				matrix[i] = (char **)realloc(matrix[i], num_cols * sizeof(char *));
				if (i == y)
					matrix[i][x] = strdup(temp);
				else
					matrix[i][x] = strdup("");
			}
			push(&head, 't', NULL, strdup(temp), 0, 0, y, x, s_y, s_x, 0, 0);
			free(temp);
		}
		if (mode == 'i') {
			y++;
		}
		if (mode == 'j') {
			x++;
		}
	}
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
	unlink(FIFO);
	exit(0);
}

void keypress(int key) {
	for (int i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
		if (key == keys[i].key) {
			(*keys[i].func)(&keys[i].arg);
		}
	}
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
	ssize_t line_size = -1;
	*num_rows = 0;
	*num_cols = 0;

	if (file != NULL)
		line_size = getline(&line_buf, &line_buf_size, file);
	if (line_size == -1 || file == NULL) {
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

		matrix[*num_rows] = split_string(line_buf, ',', num_cols, 1);

		(*num_rows)++;
		line_size = getline(&line_buf, &line_buf_size, file);
	}

	free(line_buf);
	return matrix;
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
	int error;
	if (argc > 1)
		fname = argv[1];
    FILE *file = fopen(fname, "r");
	matrix = read_to_matrix(file, &num_rows, &num_cols);
	if (file != NULL)
		fclose(file);

	if (mkfifo(FIFO, 0666) == -1) {
        if (errno != EEXIST)
            perror("mkfifo");
    }

    initscr();
    cbreak();
	raw();
    noecho();
	set_tabsize(CELL_WIDTH);
    keypad(stdscr, TRUE); // enable use of special keys as KEY_LEFT
	int key;
	int h, w;
	int step_mv = 3;

	while (1) {
		when_resize();
		draw();
		key = getch();
		keypress(key);
	}

}

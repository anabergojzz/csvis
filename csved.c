#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define CELL_WIDTH 10
#define FIFO "/bin/command_pipe"
#define XCLIP_COPY "vis-clipboard --copy"
#define XCLIP_PASTE "vis-clipboard --paste"
#define MOVE_X 3
#define MOVE_Y 5

char ***matrix;
char ***mat_reg = NULL;
int reg_rows, reg_cols = 0;
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
char *fname;

typedef union {
	char i;
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
	int rows;
	int cols;
	struct node * next;
	struct node * prev;
} node_t;
node_t * head = NULL;

size_t utf8_strlen(const char *str);
void draw();
void move_down(const Arg *arg);
void move_up(const Arg *arg);
void move_right(const Arg *arg);
void move_left(const Arg *arg);
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
void read_from_pipe(const Arg *arg);
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
void push(node_t ** head, char operation, char *** mat, char * cell, int rows, int cols, int y, int x);

static Key keys[] = {
	{'q', quit, {0}},
	{'v', visual_start, {0}},
	{'V', visual, {0}},
	{'\x03', visual_end, {0}}, //Ctrl-C
	{'j', move_down, {.i = 1}},
	{KEY_DOWN, move_down, {.i = 1}},
	{'k', move_up, {.i = 1}},
	{KEY_UP, move_up, {.i = 1}},
	{'l', move_right, {.i = 1}},
	{KEY_RIGHT, move_right, {.i = 1}},
	{'h', move_left, {.i = 1}},
	{KEY_LEFT, move_left, {.i = 1}},
	{'\x04', move_down, {.i = MOVE_Y}}, //Ctrl-D
	{'\x15', move_up, {.i = MOVE_Y}}, //Ctrl-U
	{'w', move_right, {.i = MOVE_X}},
	{'b', move_left, {.i = MOVE_X}},
	{'G', move_down, {.i = 0}},
	{'g', move_up, {.i = 0}},
	{'$', move_right, {.i = 0}},
	{'0', move_left, {.i = 0}},
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
	{'>', write_to_pipe, {'>'}},
	{'|', write_to_pipe, {'|'}},
	{'\x0F', write_to_pipe, {2}}, //Ctrl-O awk
	{'<', write_to_pipe, {'<'}},
	{'d', wipe_cells, {0}},
	{'y', yank_cells, {0}},
	{'Y', write_to_pipe, {4}},
	{'p', paste_cells, {0}},
	{'P', write_to_pipe, {5}},
	{'u', undo, {0}},
	{'\x12', redo, {0}}, //Ctrl-R
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
	for (int i = 0; i < scr_y; i++) {
		for (int j = 0; j < scr_x; j++) {
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

void move_down(const Arg *arg) {
	if (y < num_rows - arg->i && arg->i != 0) {
		if (mode == 'v') {
			if (y >= v_y)
				ch[1] = y + 1 + arg->i;
			else if (y + arg->i <= v_y)
				ch[0] = y + arg->i;
			else {
				ch[1] += arg->i - (v_y - y);
				ch[0] = v_y;
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
			if (y <= v_y)
				ch[0] = y - arg->i;
			else if (y - arg->i >= v_y)
				ch[1] = y + 1 - arg->i;
			else {
				ch[0] -= arg->i - (y - v_y);
				ch[1] = v_y + 1;
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
			if (x >= v_x)
				ch[3] = x + 1 + arg->i;
			else if (x + arg->i <= v_x)
				ch[2] = x + arg->i;
			else {
				ch[3] += arg->i - (v_x - x);
				ch[2] = v_x;
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
			if (x <= v_x)
				ch[2] = x - arg->i;
			else if (x - arg->i >= v_x)
				ch[3] = x + 1 - arg->i;
			else {
				ch[2] -= arg->i - (x - v_x);
				ch[3] = v_x + 1;
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
	push(&head, 'h', NULL, NULL, arg->i, 1, y, x);
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
	push(&head, 'g', NULL, NULL, 1, arg->i, y, x);
}

void delete_row() {
	int num = ch[1] - ch[0];
	char ***undo_mat = (char ***)malloc(num*sizeof(char**));
	if (num_rows > num) {
		for (int i = 0; i < num; i++)
			undo_mat[i] = matrix[ch[0] + i];
		for (int i = ch[0]; i < num_rows - num; i++)
			matrix[i] = matrix[i + num];
		matrix = realloc(matrix, (num_rows - num)*sizeof(char**));
		push(&head, 'e', undo_mat, NULL, num, num_cols, ch[0], 0);
		if (ch[1] == num_rows)
			y = ch[0] - 1;
		else
			y = ch[0];
		num_rows -= num;
		if (scr_y - (num_rows - s_y) > 0)
			s_y -= scr_y - (num_rows - s_y);
		if (y < s_y)
			s_y = y - 1;
		if (s_y < 0)
			s_y = 0;
		//if (s_y >= num_rows)
		//	s_y = num_rows - 1;
		//else if (num_rows <= scr_y)
		//	s_y = 0;
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
		push(&head, 'f', undo_mat, NULL, num_rows, num, 0, ch[2]);
		if (ch[3] == num_cols)
			x = ch[2] - 1;
		else
			x = ch[2];
		num_cols -= num;
		if (scr_x - (num_cols - s_x) > 0)
			s_x -= scr_x - (num_cols - s_x);
		if (x < s_x)
			s_x = x - 1;
		if (s_x < 0)
			s_x = 0;
		//if (s_x >= num_cols)
		//	s_x = num_cols - 1;
		//else if (num_cols <= scr_x)
		//	s_x = 0;
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
	int c_y0, c_x0;	

	if (cmd != 0) {
		c_y0 = c_y;
		c_x0 = c_x;
		c_x = 1;
		c_y = rows - 1;
	}

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
			if (cmd != 0)
				mvaddch(c_y, c_x-1, cmd);
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
		else if (key == KEY_RESIZE) {
			if (cmd == ':') {
				c_x = c_x0;
				c_y = c_y0;
			}
			when_resize();
			if (cmd == ':') {
				c_x = 1;
				c_y = rows - 1;
			}
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
	ch[0], ch[1], ch[2], ch[3] = 0;
	c_y = c_y0;
	c_x = c_x0;
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
			move.i = 0;
			move_right(&move);
		}
		if (key == '0') {
			Arg move;
			move.i = 0;
			move_left(&move);
		}
		if (key == 'w') {
			Arg move;
			move.i = MOVE_X;
			move_right(&move);
		}
		if (key == 'b') {
			Arg move;
			move.i = MOVE_X;
			move_left(&move);
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
			move.i = 0;
			move_down(&move);
		}
		if (key == 'g') {
			Arg move;
			move.i = 0;
			move_up(&move);
		}
		if (key == '\x04') {
			Arg move;
			move.i = MOVE_Y;
			move_down(&move);
		}
		if (key == '\x15') {
			Arg move;
			move.i = MOVE_Y;
			move_up(&move);
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
		char *fname_rel = FIFO;
		filename = (char *)malloc(strlen(getenv("HOME")) + strlen(fname_rel) + 1);
		strcpy(filename, getenv("HOME"));
		strcat(filename, fname_rel);
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

void write_to_pipe(const Arg *arg) {
	char* cmd;
	if (arg->i == 2)
		cmd = get_str("", 0, '|');
	else if (arg->i == 4) {
		cmd = malloc(30);
		strcpy(cmd, XCLIP_COPY);
	}
	else if (arg->i == 5) {
		cmd = malloc(30);
		strcpy(cmd, XCLIP_PASTE);
	}
	else
		cmd = get_str("", 0, arg->i);
    int pipefd[2];
    int pipefd2[2];
	pid_t pid;
	int status;
    ssize_t bytes_read;
	char* buffer;
	size_t buffer_size = 20;
	size_t total_bytes = 0;
	
	if (arg->i != '<' && arg->i != 5) {
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
		if (arg->i != '<' && arg->i != 5) {
			// redirect stdin to input
			close(pipefd[1]);
			dup2(pipefd[0], STDIN_FILENO);
			close(pipefd[0]);
		}

		// redirect stdout to output
        close(pipefd2[0]);
        dup2(pipefd2[1], STDOUT_FILENO);
        close(pipefd2[1]);

		char* temp = strdup(cmd);
		int num_args = 0;
		char* token = strtok(temp, " ");
		while (token != NULL) {
			num_args++;
			token = strtok(NULL, " ");
		}
		free(temp);
		char** cmd_arg = malloc((num_args+1)*sizeof(char*));
		if (cmd_arg == NULL) {
			perror("malloc failed");
			exit(1);
		}
		if (arg->i == 2) {
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

        execvp(cmd_arg[0], cmd_arg);
        // if execlp witout success
        perror(" execvp");
        exit(EXIT_FAILURE);
		free(cmd);
		free(cmd_arg);
	}
	else {  // Parent process
        close(pipefd2[1]);

		if (arg->i != '<' && arg->i != 5) {
			close(pipefd[0]);
			if (mode == 'n') {
				ch[0] = 0;
				ch[1] = num_rows;
				ch[2] = 0;
				ch[3] = num_cols;
			}

			char* str;
			for (int i = ch[0]; i < ch[1]; i++) {
				for (int j = ch[2]; j < ch[3] - 1; j++) {
					str = matrix[i][j];
					write(pipefd[1], str, strlen(str));
					write(pipefd[1], ",", 1);
				}
				str = matrix[i][ch[3] - 1];
				write(pipefd[1], str, strlen(str));
				write(pipefd[1], "\n", 1);
			}
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
				if (arg->i != '<' && arg->i != 5)
					visual_end();
				int num_cols_2, num_rows_2;
				char** temp = split_string(buffer, '\n', &num_rows_2, 0);
				char *** undo_mat = (char***)malloc(num_rows_2 * sizeof(char**));
				char *** paste_mat = (char***)malloc(num_rows_2 * sizeof(char**));
				for (int i = 0; i < num_rows_2; i++) {
					char** temp2 = split_string(temp[i], ',', &num_cols_2, 1);
					undo_mat[i] = (char**)malloc(num_cols_2 * sizeof(char*));
					paste_mat[i] = (char**)malloc(num_cols_2 * sizeof(char*));
					if (num_rows_2 <= (num_rows - y) && num_cols_2 <= (num_cols - x)) {
						for (int j = 0; j < num_cols_2; j++) {
							undo_mat[i][j] = matrix[y + i][x + j];
							paste_mat[i][j] = strdup(temp2[j]);
							matrix[y + i][x + j] = strdup(temp2[j]);
						}
					}
					free(temp2);
				}
				if (num_rows_2 <= (num_rows - y) && num_cols_2 <= (num_cols - x)) {
					push(&head, 'p', undo_mat, NULL, num_rows_2, num_cols_2, y, x);
					push(&head, 'p', paste_mat, NULL, num_rows_2, num_cols_2, y, x);
				}
				free(temp);
			}
			free(buffer);
		}
		else if (arg->i == 4)
			visual_end();
    }

	c_y = c_y0;
	c_x = c_x0;
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
		push(&head, 'd', undo_mat, NULL, reg_rows, reg_cols, ch[0], ch[2]);

		visual_end();
	}
}

void push(node_t ** head, char operation,  char *** mat, char * cell, int rows, int cols, int y, int x) {
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
			for (int i=head->y; i<(head->y + head->rows); i++) {
				for (int j=head->x; j<(head->x + head->cols); j++) {
					free(matrix[i][j]);
					matrix[i][j] = strdup("");
				}
			}
			head = head->next;
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
			head = head->next;
			free(matrix[head->y][head->x]);
			matrix[head->y][head->x] = strdup(head->cell);
		}
		else if (head->operation == 'e') {
			for (int i = num_rows; i > head->y; i--) {
				matrix[i] = matrix[i - 1];
			}
			num_rows++;
			matrix[head->y] = (char **)malloc(num_cols * sizeof(char *));
			for (int j = 0; j < num_cols; j++) {
				matrix[head->y][j] = strdup(head->mat[0][j]);
			}
		}
		else if (head->operation == 'f') {
			for (int i = 0; i < num_rows; i++) {
				matrix[i] = (char **)realloc(matrix[i], (num_cols+1) * sizeof(char *));
				for (int j = num_cols; j > head->x; j--) {
					matrix[i][j] = matrix[i][j - 1];
				}
				matrix[i][head->x] = strdup(head->mat[i][0]);
			}
			num_cols++;
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
		y = head->y;
		x = head->x;
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
			for (int i=head->y; i<(head->y + head->rows); i++) {
				for (int j=head->x; j<(head->x + head->cols); j++) {
					free(matrix[i][j]);
					matrix[i][j] = strdup("");
				}
			}
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
			for (int i = 0; i < num_cols; i++)
				free(matrix[head->y][i]);
			free(matrix[head->y]);

			for (int i = head->y; i < num_rows - 1; i++)
				matrix[i] = matrix[i + 1];

			matrix[num_rows - 1] = NULL;
			num_rows--;
		}
		else if (head->operation == 'f') {
			for (int j = 0; j < num_rows; j++) {
				free(matrix[j][head->x]);
				for (int i = head->x; i < num_cols - 1; i++)
					matrix[j][i] = matrix[j][i + 1];
				matrix[j][num_cols - 1] = NULL;
			}
			num_cols--;
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
		y = head->y;
		x = head->x;
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
			push(&head, 'p', undo_mat, NULL, reg_rows, reg_cols, y, x);
			push(&head, 'p', paste_mat, NULL, reg_rows, reg_cols, y, x);
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

		push(&head, 'r', NULL, undo_cell, 0, 0, y, x);
		push(&head, 'r', NULL, paste_cell, 0, 0, y, x);
		if (mode == 'i') {
			if (y >= num_rows - 1) {
				Arg insert;
				insert.i = 1;
				insert_row(&insert);
			}
			y++;
			when_resize();
		}
		if (mode == 'j') {
			if (x >= num_cols - 1) {
				Arg insert;
				insert.i = 1;
				insert_col(&insert);
			}
			x++;
			when_resize();
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

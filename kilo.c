// Created by Julian Carter on 2/13/25.
///*** Includes***/
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <string.h>
/*** Defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define KILO_VERSION "0.0.1"
/*** Data ***/
struct editorConfig {
    int cx; int cy;
    struct termios orig_termios;
    int screenRows;
    int screenCols;
};

struct editorConfig E;

/*** Terminal ***/
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror("DEAD!");
    exit(1);
}

void disable_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
        die("tcsetattr");
    }
}

void enable_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tccgetattr");
    atexit(disable_raw_mode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cflag &= (CS8);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(0, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b') {
        char seq [3];

        if (read(STDIN_FILENO, &seq[0],1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1],1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (c) {
                case 'A': return 'w';
                case 'B': return 's';
                case 'C': return 'd';
                case 'D': return 'a';
            }
        }
        return '\x1b';
    }else {
        return c;
    }
}

int getCursorPosition(int *row, int *cols) {
    char buf[32];
    unsigned int i;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) return -1;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] ='\0';

    if (buf[0] != '\x1b'|| buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", row, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** Append Buffer ***/
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL,0}

void abAppend(struct abuf *ab, const char *s, int len) {
char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}


void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        if (y == E.screenRows / 3) {
            char welcome[82];
            int welcomeLen = snprintf(welcome, sizeof(welcome), "Kilo Editor -- version %s", KILO_VERSION);
            if (welcomeLen > E.screenCols) welcomeLen = E.screenCols;
            int padding = (E.screenCols - welcomeLen)/2;
            if (padding) {
                abAppend(ab,"~", 1);
                padding--;
            }while (padding--) abAppend(ab, " ",1);
            abAppend(ab, welcome, welcomeLen);
        }else {
            abAppend(ab,"~", 1);
        }

        abAppend(ab, "\x1b[K", 3);
        if (y > E.screenRows -1) abAppend(ab, "\r\n", 2);
    }
}
/*** Output ***/
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy +1, E.cx +1);
    abAppend(&ab,buf,strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
/*** Input ***/
void editorMoveKey(char key) {
    switch (key) {
        case 'a':
            E.cx--;
            break;
        case 'd':
            E.cx++;
            break;
        case 'w':
            E.cy--;
            break;
        case 's':
            E.cy++;
            break;
    }
}

void editorProcessKeyPressed() {
    char c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;


        case 'w':
        case 's':
        case 'a':
        case 'd':
        editorMoveKey(c);
        break;
    }
}
/*** innit ***/
void innitEditor() {
    E.cx = 0;
    E.cy = 0;
    if (getWindowSize(&E.screenRows, &E.screenCols) == 1) die("getWindowSize");
}

int main(void) {
    enable_raw_mode();
    innitEditor();
    while (1) {
        editorRefreshScreen();
        editorProcessKeyPressed();
    }
    return 0;
}



/**
 * shell.cpp — task_shell: a tiny Plan 9/rc-flavored interactive shell
 *
 * Polls stdio non-blocking (getchar_timeout_us(0)), so it fits the
 * cooperative model with no scheduler changes. Echoes what's typed and
 * dispatches complete lines against the /dev namespace (kernel/fs.h).
 * Talks to stdio directly rather than through klog -- interactive echo
 * needs to be immediate, not batched behind a flush task.
 *
 * Commands: ls, cat <path>, write <path> <text...>. Deliberately no
 * pipes/redirection yet -- this is the seed, not the shell.
 */
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>

#define LINE_MAX 80

static void print_prompt(void)
{
    printf("%% ");
}

static void cmd_ls(void)
{
    int n = fs_count();
    for (int i = 0; i < n; i++) {
        printf("%s\n", fs_name(i));
    }
}

static void cmd_cat(char *path)
{
    if (!path) {
        printf("usage: cat <path>\n");
        return;
    }
    int fd = fs_open(path);
    if (fd < 0) {
        printf("cat: no such device: %s\n", path);
        return;
    }
    char buf[64];
    int n = fs_read(fd, buf, sizeof(buf) - 1);
    fs_close(fd);
    if (n < 0) {
        printf("cat: %s is not readable\n", path);
        return;
    }
    buf[n] = '\0';
    printf("%s", buf);
    if (n == 0 || buf[n - 1] != '\n') printf("\n");
}

static void cmd_write(char *path, char *text)
{
    if (!path || !text) {
        printf("usage: write <path> <text>\n");
        return;
    }
    int fd = fs_open(path);
    if (fd < 0) {
        printf("write: no such device: %s\n", path);
        return;
    }
    int n = fs_write(fd, text, strlen(text));
    fs_close(fd);
    if (n < 0) {
        printf("write: %s rejected \"%s\"\n", path, text);
    }
}

static void dispatch(char *line)
{
    char *cmd = strtok(line, " \t");
    if (!cmd) return; // empty line

    if (strcmp(cmd, "ls") == 0) {
        cmd_ls();
    } else if (strcmp(cmd, "cat") == 0) {
        cmd_cat(strtok(0, " \t"));
    } else if (strcmp(cmd, "write") == 0) {
        char *path = strtok(0, " \t");
        char *text = strtok(0, ""); // rest of the line, one token
        cmd_write(path, text);
    } else {
        printf("unknown command: %s\n", cmd);
    }
}

void task_shell(void)
{
    static char line[LINE_MAX];
    static int  line_len = 0;
    static bool banner_shown = false;

    if (!banner_shown) {
        printf("\npicoos -- type 'ls' to see /dev\n");
        print_prompt();
        banner_shown = true;
    }

    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[line_len] = '\0';
            dispatch(line);
            line_len = 0;
            print_prompt();
        } else if (c == 8 || c == 127) { // backspace / DEL
            if (line_len > 0) {
                line_len--;
                printf("\b \b");
            }
        } else if (line_len < LINE_MAX - 1) {
            line[line_len++] = (char)c;
            putchar(c);
        }
    }
}

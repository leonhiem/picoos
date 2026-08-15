#include "jobs.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include "kernel/prog.h"
#include <cstdio>
#include <cstring>

void pipeline_run_once(pipeline_t *p)
{
    char buf_a[STAGE_BUF];
    char buf_b[STAGE_BUF];
    char *cur = buf_a, *other = buf_b;
    int cur_len = 0;

    if (p->has_redirect_in) {
        int fd = fs_open(p->redirect_in);
        if (fd < 0) { printf("%s: no such device\n", p->redirect_in); return; }
        int n = fs_read(fd, cur, STAGE_BUF);
        fs_close(fd);
        cur_len = n < 0 ? 0 : n;
    }

    for (int i = 0; i < p->nstages; i++) {
        pipeline_stage_t *st = &p->stages[i];
        if (st->argc == 0) { printf("empty pipeline stage\n"); return; }

        const program_t *prog = prog_find(st->argv[0]);
        if (!prog) { printf("%s: not found (try 'ls')\n", st->argv[0]); return; }

        char *argv_rest[JOB_MAX_ARGS];
        for (int a = 1; a < st->argc; a++) argv_rest[a - 1] = st->argv[a];

        int n = prog->run(cur, cur_len, st->argc - 1, argv_rest, other, STAGE_BUF);
        if (n < 0) { printf("%s: error\n", st->argv[0]); return; }

        char *tmp = cur; cur = other; other = tmp;
        cur_len = n;
    }

    if (p->has_redirect_out) {
        int fd = fs_open(p->redirect_out);
        if (fd < 0) { printf("%s: no such device\n", p->redirect_out); return; }
        fs_write(fd, cur, cur_len);
        fs_close(fd);
    } else {
        for (int i = 0; i < cur_len; i++) putchar(cur[i]);
        if (cur_len == 0 || cur[cur_len - 1] != '\n') putchar('\n');
    }
}

typedef struct {
    bool active;
    pipeline_t p;
} job_slot_t;

static job_slot_t slots[MAX_JOBS];

static void run_slot(int i)
{
    if (!slots[i].active) return;
    pipeline_run_once(&slots[i].p);
}

static void job_task_0(void) { run_slot(0); }
static void job_task_1(void) { run_slot(1); }
static void job_task_2(void) { run_slot(2); }
static void job_task_3(void) { run_slot(3); }

void jobs_init(void)
{
    for (int i = 0; i < MAX_JOBS; i++) slots[i].active = false;
    task_register("job1", job_task_0, 1000);
    task_register("job2", job_task_1, 1000);
    task_register("job3", job_task_2, 1000);
    task_register("job4", job_task_3, 1000);
}

int job_start(const pipeline_t *p)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!slots[i].active) {
            slots[i].p = *p;
            slots[i].active = true;
            return i + 1;
        }
    }
    return -1;
}

void job_list(void)
{
    bool any = false;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!slots[i].active) continue;
        any = true;
        printf("[%d] ", i + 1);
        for (int s = 0; s < slots[i].p.nstages; s++) {
            if (s > 0) printf("| ");
            for (int a = 0; a < slots[i].p.stages[s].argc; a++)
                printf("%s ", slots[i].p.stages[s].argv[a]);
        }
        if (slots[i].p.has_redirect_out) printf("> %s ", slots[i].p.redirect_out);
        printf("&\n");
    }
    if (!any) printf("no background jobs\n");
}

bool job_kill(int id)
{
    if (id < 1 || id > MAX_JOBS || !slots[id - 1].active) return false;
    slots[id - 1].active = false;
    return true;
}

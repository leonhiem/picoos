/**
 * jobs.h — pipeline execution + background job control
 *
 * A pipeline is a chain of program stages (kernel/prog.h) with optional
 * `<`/`>` device redirects. pipeline_run_once() runs it synchronously
 * once, either printing the result to the terminal or writing it to a
 * `>` device.
 *
 * Backgrounding (`&`) doesn't fork -- there's no process model here,
 * just kernel/task.h's cooperative scheduler. A fixed pool of MAX_JOBS
 * slots is registered as real tasks once, at boot (jobs_init()), each
 * normally a no-op; starting a job fills a slot and it starts running
 * on its own schedule, "killing" it just flips the slot back to
 * inactive. kernel/task.h itself is untouched -- no task_unregister()
 * needed, and no way to pass a slot index through its plain function
 * pointer, so the trampolines are hand-duplicated one per slot. Fine
 * at MAX_JOBS=4; a context-carrying task_register() would be worth it
 * if this ever needed to grow much.
 */
#pragma once

#define JOB_MAX_STAGES 4
#define JOB_MAX_ARGS   4
#define JOB_TOK_MAX    24
#define MAX_JOBS       4
#define STAGE_BUF      256

typedef struct {
    char argv[JOB_MAX_ARGS][JOB_TOK_MAX];
    int  argc;
} pipeline_stage_t;

typedef struct {
    int  nstages;
    pipeline_stage_t stages[JOB_MAX_STAGES];
    bool has_redirect_out;
    char redirect_out[JOB_TOK_MAX];
    bool has_redirect_in;
    char redirect_in[JOB_TOK_MAX];
} pipeline_t;

void pipeline_run_once(pipeline_t *p);

void jobs_init(void);
int  job_start(const pipeline_t *p);  // returns job id (1..MAX_JOBS), or -1 if all slots busy
void job_list(void);
bool job_kill(int id);

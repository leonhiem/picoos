/**
 * shell.cpp — task_shell: a tiny Plan 9/rc-flavored interactive shell
 *
 * Polls stdio non-blocking (getchar_timeout_us(0)), so it fits the
 * cooperative model with no scheduler changes. Echoes what's typed and
 * dispatches complete lines. Talks to stdio directly rather than
 * through klog -- interactive echo needs to be immediate, not batched
 * behind a flush task.
 *
 * Grammar: stage [| stage ...] [< path] [> path] [&]
 *   stage := progname [arg...]
 * `jobs`, `kill %<id>`, `sleep <ms>`, and `script`/`run`/`scripts` (see
 * below) are true shell builtins (they touch the shell's own state,
 * not a text-in/text-out stream); everything else -- including ls now
 * -- is a program (kernel/prog.h) run through the same pipeline
 * machinery (jobs.h), foreground or backgrounded.
 *
 * `sleep` doesn't block -- nothing in this kernel ever does. It just
 * remembers a wake time; task_shell() checks it at the top of every
 * one of its own 30ms ticks and returns immediately (a few
 * microseconds) if not yet time, same as any other task "waiting"
 * for its next scheduled run. Everything else (jobs, tpo, setpoint
 * polling) keeps running completely normally in the meantime -- this
 * is the existing cooperative model, not a new kernel primitive.
 * Characters typed during a sleep aren't lost (or echoed) until it
 * ends -- they just sit in the USB stack's own RX buffer until
 * getchar_timeout_us() starts being called again.
 *
 * `script <name>` / `run <name>` -- named, storable sequences of shell
 * lines, the small step toward real scripts. `script <name>` puts the
 * shell into capture mode: the prompt changes to `> ` and every line
 * typed gets appended to that script instead of being run, until a
 * line containing just `.` ends it (an old ed-style terminator --
 * chosen over inventing `>>`/heredoc grammar). `scripts` lists what's
 * stored.
 *
 * `run <name>` replays the stored lines through the same dispatch()
 * every typed line goes through -- but one line per task_shell() tick,
 * not all at once, and it is genuinely driven by the same resumable
 * `sleeping` flag `sleep` already uses: if a script line is itself
 * `sleep <ms>`, run_step() (below) simply doesn't get called again
 * until that sleep's wake time has passed, same as if you'd typed it
 * by hand. This isn't optional plumbing -- a first version dispatched
 * every line in one synchronous loop, which meant `sleep` inside a
 * script did nothing at all (it only ever sets a flag; something else
 * has to actually wait on it), so a heater or lamp step "after" a
 * sleep line fired immediately instead of after the delay.
 *
 * Storage is a small RAM table (`scripts[SCRIPT_MAX]`) -- deliberately
 * RAM-only for now: this board has no EEPROM, so nothing survives a
 * power cycle yet, and there's no boot-time auto-run. When persistent
 * storage exists (onboard flash, or a real EEPROM on a future PCB),
 * the plan is to swap what backs `scripts[]` without changing
 * `script`/`run`/`scripts` themselves -- same reasoning as every
 * device in dev/*.cpp, just not wired to a device path (yet).
 *
 * `watch <ms> <stage> [| stage ...] [< path]` -- repeats a pipeline in
 * the foreground every <ms>, printing every time, until Ctrl-C (ASCII
 * ETX, 0x03). This is real Unix's own `watch`, not a new idea: for
 * something like a monitor line you actually want to watch and then
 * stop watching, backgrounding it with `&` is the wrong tool -- a
 * background job stays quiet (no `>` redirect means no spam, on
 * purpose, for things like `toggle &`), which is exactly backwards for
 * a monitor that's supposed to print every tick, and the only way to
 * stop a background job is `kill %<id>`, remembering an id, instead of
 * the muscle-memory Ctrl-C this exists to give back. `&` inside a
 * watched pipeline is rejected -- watch already *is* the repeat.
 *
 * Ctrl-C is scoped to `watch` alone, not generalized to interrupt
 * `sleep`/`run` too: while watching, every byte is actively polled and
 * discarded except 0x03, which is a real, deliberate behavior change
 * from the normal loop (nothing is preserved for after, the same way
 * real `watch` doesn't queue up your keystrokes) -- fine to introduce
 * for a state that starts brand new in this commit, not something to
 * retrofit onto sleep/run's existing, already-relied-on "nothing
 * typed is ever lost" contract.
 *
 * A "boot" script comes pre-loaded into scripts[0] -- baked into the
 * firmware image itself (BOOT_SCRIPT_TEXT below), not written to any
 * storage, so it's back after every reboot without needing EEPROM or
 * onboard-flash persistence to exist first (still neither exists on
 * this board -- see the RAM-only note above, still true for anything
 * *captured* interactively). `run boot` starts it. Deliberately not
 * auto-run at boot without being asked -- this wiring starts real
 * heater control, and that shouldn't begin unattended.
 */
#include "kernel/fs.h"
#include "jobs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define LINE_MAX    96
#define MAX_TOKENS  20

#define SCRIPT_MAX       4
#define SCRIPT_NAME_MAX  16
#define SCRIPT_TEXT_MAX  1024 // a handful of lines' worth; independent of
                               // jobs.h's STAGE_BUF -- different concern
                               // (script storage, not ls's output). Bumped
                               // from 512 -- BOOT_SCRIPT_TEXT below alone
                               // is ~480 bytes, too close to that cap to
                               // leave any real room to grow it further.

typedef struct {
    bool used;
    char name[SCRIPT_NAME_MAX];
    char text[SCRIPT_TEXT_MAX]; // lines joined by '\n', nul-terminated
} script_t;

// The default wiring for full manual+auto heater control (boost/coast/
// pid/safe phases) plus the UI jobs (button toggles, display follows) --
// exactly the recipe from README.md's "Full heater control" section
// (alarmcheck/alarm included), each line backgrounded with its own
// trailing '&' since run_step()
// replays these through the same dispatch() a typed line goes through.
#define BOOT_SCRIPT_TEXT \
    "follow /dev/heaterauto /dev/setpoint /dev/percent /dev/seg7small &\n" \
    "toggle /dev/buttons/manual /dev/heaterauto &\n" \
    "adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &\n" \
    "phase &\n" \
    "select /dev/state boost=80 coast=0 pid=/dev/pidout safe=/dev/safepower > /dev/autopower &\n" \
    "cat /dev/skintemp | pid /dev/state pid /dev/setpoint > /dev/pidout &\n" \
    "cat /dev/ambient | safelut > /dev/safepower &\n" \
    "follow /dev/heaterauto /dev/autopower /dev/percent /dev/heater &\n" \
    "cat /dev/skintemp > /dev/seg7big &\n" \
    "alarmcheck &\n" \
    "alarm /dev/alarm/heater /dev/alarm/temphigh /dev/alarm/templow &\n"

static script_t scripts[SCRIPT_MAX] = {
    { true, "boot", BOOT_SCRIPT_TEXT },
};

static bool sleeping = false;
static absolute_time_t wake_time;

static bool capturing = false;
static char capture_name[SCRIPT_NAME_MAX];
static char capture_buf[SCRIPT_TEXT_MAX];
static int  capture_len = 0;

static bool running = false;      // a script is being replayed, one line per tick
static char run_buf[SCRIPT_TEXT_MAX];
static int  run_pos = 0;          // offset of the next unread line in run_buf

static bool watching = false;
static pipeline_t watch_pipeline;
static uint32_t watch_interval_ms;
static absolute_time_t watch_next;

static void print_prompt(void)
{
    printf(capturing ? "> " : "%% ");
}

static int tokenize(char *line, char *tokens[MAX_TOKENS])
{
    int n = 0;
    char *tok = strtok(line, " \t");
    while (tok && n < MAX_TOKENS) {
        tokens[n++] = tok;
        tok = strtok(0, " \t");
    }
    return n;
}

static bool parse_pipeline(char **tok, int ntok, pipeline_t *p, bool *background)
{
    p->nstages = 1;
    p->stages[0].argc = 0;
    p->has_redirect_out = false;
    p->has_redirect_in = false;
    *background = false;

    pipeline_stage_t *stage = &p->stages[0];

    for (int i = 0; i < ntok; i++) {
        const char *t = tok[i];
        if (strcmp(t, "|") == 0) {
            if (p->nstages >= JOB_MAX_STAGES) { printf("too many pipeline stages\n"); return false; }
            stage = &p->stages[p->nstages++];
            stage->argc = 0;
        } else if (strcmp(t, ">") == 0) {
            if (++i >= ntok) { printf("> needs a path\n"); return false; }
            strncpy(p->redirect_out, tok[i], JOB_TOK_MAX - 1);
            p->redirect_out[JOB_TOK_MAX - 1] = '\0';
            p->has_redirect_out = true;
        } else if (strcmp(t, "<") == 0) {
            if (++i >= ntok) { printf("< needs a path\n"); return false; }
            strncpy(p->redirect_in, tok[i], JOB_TOK_MAX - 1);
            p->redirect_in[JOB_TOK_MAX - 1] = '\0';
            p->has_redirect_in = true;
        } else if (strcmp(t, "&") == 0) {
            *background = true;
        } else {
            if (stage->argc >= JOB_MAX_ARGS) { printf("too many args in one stage\n"); return false; }
            strncpy(stage->argv[stage->argc], t, JOB_TOK_MAX - 1);
            stage->argv[stage->argc][JOB_TOK_MAX - 1] = '\0';
            stage->argc++;
        }
    }
    return true;
}

// Appends one typed line to the script currently being captured, or
// finalizes it on a lone ".". Not called while a script is running --
// run replays through dispatch(), never through capture.
static void capture_line(const char *line)
{
    if (strcmp(line, ".") == 0) {
        capture_buf[capture_len] = '\0';
        int slot = -1;
        for (int i = 0; i < SCRIPT_MAX; i++) {
            if (scripts[i].used && strcmp(scripts[i].name, capture_name) == 0) { slot = i; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < SCRIPT_MAX; i++) {
                if (!scripts[i].used) { slot = i; break; }
            }
        }
        if (slot < 0) {
            printf("script: table full (max %d), '%s' not saved\n", SCRIPT_MAX, capture_name);
        } else {
            strncpy(scripts[slot].name, capture_name, SCRIPT_NAME_MAX - 1);
            scripts[slot].name[SCRIPT_NAME_MAX - 1] = '\0';
            strncpy(scripts[slot].text, capture_buf, SCRIPT_TEXT_MAX - 1);
            scripts[slot].text[SCRIPT_TEXT_MAX - 1] = '\0';
            scripts[slot].used = true;
            printf("script '%s' saved (%d bytes)\n", capture_name, capture_len);
        }
        capturing = false;
        return;
    }

    int llen = strlen(line);
    if (capture_len + llen + 1 >= SCRIPT_TEXT_MAX) { // +1 for the '\n' this line adds
        printf("script: buffer full, line dropped\n");
        return;
    }
    memcpy(capture_buf + capture_len, line, llen);
    capture_len += llen;
    capture_buf[capture_len++] = '\n';
}

static void dispatch(char *line)
{
    char *tok[MAX_TOKENS];
    int ntok = tokenize(line, tok);
    if (ntok == 0) return; // empty line

    if (strcmp(tok[0], "jobs") == 0) {
        job_list();
        return;
    }
    if (strcmp(tok[0], "kill") == 0) {
        if (ntok < 2) { printf("usage: kill %%<jobid>\n"); return; }
        const char *arg = tok[1];
        if (*arg == '%') arg++;
        if (!job_kill(atoi(arg))) printf("kill: no such job\n");
        return;
    }
    if (strcmp(tok[0], "sleep") == 0) {
        if (ntok < 2) { printf("usage: sleep <ms>\n"); return; }
        int ms = atoi(tok[1]);
        if (ms < 0) ms = 0;
        wake_time = make_timeout_time_ms(ms);
        sleeping = true;
        return; // no prompt yet -- task_shell() prints one once awake
    }
    if (strcmp(tok[0], "script") == 0) {
        if (ntok < 2) { printf("usage: script <name>\n"); return; }
        strncpy(capture_name, tok[1], SCRIPT_NAME_MAX - 1);
        capture_name[SCRIPT_NAME_MAX - 1] = '\0';
        capture_len = 0;
        capturing = true;
        printf("capturing '%s' -- end with a line containing just '.'\n", capture_name);
        return; // prompt becomes "> " until capture ends
    }
    if (strcmp(tok[0], "scripts") == 0) {
        bool any = false;
        for (int i = 0; i < SCRIPT_MAX; i++) {
            if (scripts[i].used) { printf("%s\n", scripts[i].name); any = true; }
        }
        if (!any) printf("no scripts\n");
        return;
    }
    if (strcmp(tok[0], "run") == 0) {
        if (ntok < 2) { printf("usage: run <name>\n"); return; }
        if (running) { printf("run: already running a script\n"); return; }
        int slot = -1;
        for (int i = 0; i < SCRIPT_MAX; i++) {
            if (scripts[i].used && strcmp(scripts[i].name, tok[1]) == 0) { slot = i; break; }
        }
        if (slot < 0) { printf("run: no such script '%s'\n", tok[1]); return; }
        strncpy(run_buf, scripts[slot].text, SCRIPT_TEXT_MAX - 1);
        run_buf[SCRIPT_TEXT_MAX - 1] = '\0';
        run_pos = 0;
        running = true;
        return; // task_shell() drives it from here, one line per tick -- see run_step()
    }
    if (strcmp(tok[0], "watch") == 0) {
        if (ntok < 3) { printf("usage: watch <ms> <stage> [| stage ...] [< path]\n"); return; }
        int ms = atoi(tok[1]);
        if (ms < 50) ms = 50; // sane floor -- also protects against a typo like "watch heater"
        bool background;
        if (!parse_pipeline(tok + 2, ntok - 2, &watch_pipeline, &background)) return;
        if (watch_pipeline.stages[0].argc == 0) return;
        if (background) printf("watch: '&' ignored -- watch already repeats on its own\n");
        watch_interval_ms = (uint32_t)ms;
        watch_next = get_absolute_time(); // fire the first line immediately
        watching = true;
        printf("watching every %dms -- Ctrl-C to stop\n", ms);
        return; // no normal prompt -- watch prints its own lines until stopped
    }
    if (tok[0][0] == '\0') return;
    if (strcmp(tok[0], "|") == 0 || strcmp(tok[0], ">") == 0) {
        printf("empty pipeline\n");
        return;
    }

    pipeline_t p;
    bool background;
    if (!parse_pipeline(tok, ntok, &p, &background)) return;
    if (p.stages[0].argc == 0) return; // e.g. line was just "&"

    if (background) {
        int id = job_start(&p);
        if (id < 0) printf("no free job slots (max %d)\n", MAX_JOBS);
        else printf("[%d]\n", id);
    } else {
        pipeline_run_once(&p, true); // foreground: always show the result
    }
}

// Runs exactly one line of the in-progress script, advancing run_pos
// past it, and clears `running` once the script is out of lines.
// Called at most once per task_shell() tick -- if the line dispatches
// to `sleep`, this simply isn't called again until that sleep's wake
// time passes (same top-of-function check `sleeping` already gets),
// so a script's sleep behaves exactly like a typed one.
static void run_step(void)
{
    if (run_buf[run_pos] == '\0') { running = false; return; }

    char *ln = run_buf + run_pos;
    char *nl = strchr(ln, '\n');
    if (nl) { *nl = '\0'; run_pos = (int)(nl - run_buf) + 1; }
    else     { run_pos += strlen(ln); } // last line (shouldn't lack a trailing '\n', but safe)

    printf("%% %s\n", ln);
    dispatch(ln);
}

void task_shell(void)
{
    static char line[LINE_MAX];
    static int  line_len = 0;
    static bool banner_shown = false;

    if (!banner_shown) {
        printf("\npicoos\n");
        printf("  ls            list /dev and bin/\n");
        printf("  jobs, kill    manage background pipelines\n");
        printf("  script, run   record and replay command sequences\n");
        printf("  watch         repeat a pipeline every <ms>, Ctrl-C to stop\n");
        print_prompt();
        banner_shown = true;
    }

    if (sleeping) {
        if (absolute_time_diff_us(get_absolute_time(), wake_time) > 0) return; // not yet
        sleeping = false;
        if (!running) print_prompt(); // a script's own sleep resumes silently, no interactive prompt
        // fall through: either process input that arrived while asleep,
        // or (if running) let the block below continue the script now
    }

    if (running) {
        run_step();
        if (!running && !sleeping) print_prompt(); // script just finished, and didn't end mid-sleep
        return; // script lines advance one per tick; don't also read keyboard input this tick
    }

    if (watching) {
        if (absolute_time_diff_us(get_absolute_time(), watch_next) <= 0) {
            pipeline_run_once(&watch_pipeline, true); // foreground: always prints
            watch_next = make_timeout_time_ms(watch_interval_ms);
        }
        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            if (c == 3) { // Ctrl-C
                watching = false;
                printf("(watch stopped)\n");
                print_prompt();
                break;
            }
            // anything else typed while watching is discarded, on purpose --
            // see the header comment for why this doesn't preserve type-ahead
        }
        return; // watching owns this tick; don't also run the normal command loop
    }

    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[line_len] = '\0';
            if (capturing) capture_line(line);
            else dispatch(line);
            line_len = 0;
            // sleep/run/watch set their own flag; each prints its own prompt once done instead
            if (!sleeping && !running && !watching) print_prompt();
        } else if (c == 8 || c == 127) { // backspace / DEL
            if (line_len > 0) {
                line_len--;
                printf("\b \b");
            }
        } else if (c >= 0x20 && c < 0x7f && line_len < LINE_MAX - 1) {
            // Printable ASCII only -- silently drop anything else (e.g.
            // stray bytes from USB CDC enumeration/terminal noise right
            // at connect time, which is exactly what showed up as a
            // garbage glyph right after the boot prompt).
            line[line_len++] = (char)c;
            putchar(c);
        }
    }
}

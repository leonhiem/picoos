# picoos

An experimental, Plan 9-flavored little OS for the RP2040, built by
gutting a working infant-warmer control loop (see
[babywarmer](https://github.com/leonhiem/babywarmer), the stable
sibling this was forked from) and replacing it with: a cooperative
task scheduler, a device namespace where hardware is just files, an
interactive shell, and pipes to wire small programs and devices
together.

No hardcoded control loop. No `ioctl()`. Everything is a file: `open`,
`close`, `read`, `write`. Small pieces, composed from the command line
— PID included: it's `bin/pid`, one more program, not a special case.

## Philosophy

- **Everything is a file.** Hardware (`/dev/skintemp`, `/dev/lamp`,
  `/dev/heater`, ...) and small programs (`bin/cat`, `bin/thresh`,
  ...) are both just named, `open`able things — `ls` lists them
  together, in one namespace.
- **The namespace is the wiring.** Nothing is a hardcoded control
  loop, not even PID (`bin/pid`, tunable live through `/dev/pid/*`).
  Control behavior is built by piping small pieces together from the
  shell, e.g. a live thermostat is
  `cat /dev/skintemp | hyst /dev/setpoint 1.0 > /dev/heater &` — not a
  function anywhere in the source.
- **Cooperative, not preemptive.** One core, one scheduler
  (`kernel/task.h`), tasks that run briefly and return. "Blocking"
  (`sleep`, background jobs waiting for a button) is built as resumable
  state checked on each task's own tick, never a real blocking call —
  see `kernel/task.h`'s comments for why.
- **Stay in Unix vocabulary, not Plan 9's own jargon** — `open`/
  `close`/`read`/`write`, not `Chan`. Small tools, not a scripting
  language (yet).

## Building

Slackware-specific toolchain paths (adjust for your system):

```sh
export PICO_SDK_PATH=/home/leon/pico/pico-sdk
export PICO_TOOLCHAIN_PATH=/home/leon/pico/pico-sdk/toolchain/gcc-arm-none-eabi-10.3-2021.10
export CMAKE_FIND_ROOT_PATH=$PICO_TOOLCHAIN_PATH
export CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
export CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
export CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY

mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Flash `build/picoos.uf2` the usual way (hold BOOTSEL, plug in USB-C,
copy the file onto the mass-storage device that appears). Console is
the same USB-C connection, standard serial terminal, 115200 baud (or
just USB CDC — no specific baud rate is enforced).

## Using the shell

Connect a serial terminal to the board. You'll get a `%` prompt (an
`rc`/Plan 9 nod):

```
picoos
  ls            list /dev and bin/
  jobs, kill    manage background pipelines
  script, run   record and replay command sequences
  watch         repeat a pipeline every <ms>, Ctrl-C to stop
%
```

Grammar:

```
stage [| stage ...] [< path] [> path] [&]
stage := progname [arg...]
```

- `ls` — list every device and program.
- `cat <path>` — read a device once.
- `echo <text...> > <path>` — write to a device.
- `prog1 | prog2 | ...` — pipe a program's output into the next stage.
- `< path`, `> path` — read a device as the first stage's input /
  write the last stage's output to a device, instead of the terminal.
- `... &` — run the pipeline as a background job instead of once. This
  doesn't fork a process (there isn't one) — it registers the pipeline
  as a real periodic task, polled every `JOB_POLL_MS` (150ms).
- `jobs` — list running background jobs.
- `kill %<id>` — stop one.
- `sleep <ms>` — pause the shell (doesn't waste CPU; everything else
  keeps running).
- `script <name>` — capture the lines you type next into a named
  script, until a line containing just `.`. Prompt becomes `> ` while
  capturing.
- `run <name>` — replay a captured script, one line per shell tick.
  `sleep` inside a script really waits (the same resumable flag as a
  typed `sleep`), and `&` inside a script backgrounds a job exactly
  like typing it directly.
- `scripts` — list captured script names.
- `watch <ms> <stage> [| stage ...] [< path]` — repeats a pipeline in
  the foreground every `<ms>`, printing every time, until **Ctrl-C**
  stops it. This is real Unix's own `watch` — the right tool for
  something like a status monitor, which you actually want to *watch*
  and then stop, not a quiet background job you'd have to remember a
  `kill %<id>` for. `&` inside a watched pipeline is rejected — watch
  already *is* the repeat.

`jobs`, `kill`, `sleep`, `script`, `run`, `scripts`, and `watch` are
**shell builtins**, not entries in the device/program namespace — same
as `cd`/`export` in a real Unix shell not showing up on `$PATH`. They
touch the shell's own state rather than being a text-in/text-out
program, so they intentionally don't appear in `ls`'s listing below.
Scripts you `script`/capture yourself are RAM-only (this board has no
EEPROM) — they don't survive a reboot. One script, `boot`, is the
exception: it's baked into the firmware image itself
(`BOOT_SCRIPT_TEXT` in `shell.cpp`), so it's back fresh after every
reboot with nothing to retype. **It also runs automatically**, before
the first prompt ever appears — real heater control starts unattended
at every power-up as a result, a deliberate choice, not an
oversight. See [Recipes](#recipes) for what it wires up.

Ctrl-C only interrupts `watch` — while watching, every byte is
actively read and discarded except Ctrl-C, which is a deliberate
behavior change from the shell's normal "nothing typed is ever lost"
rule (used by `sleep`/`run`, unaffected). Fine for a state that starts
fresh with nothing relying on it yet.

Backspace works. Unknown commands, bad paths, and rejected writes
print a short error instead of doing something silently wrong.

## Devices (`/dev/...`)

| Device | R/W | What it does |
|---|---|---|
| `/dev/skintemp` | read | Live skin-temperature reading (ADS1115/NTC), °C. Fresh value every read. |
| `/dev/ambient` | read | Live room-temperature reading, same ADS1115, other differential channel. |
| `/dev/current` | read | Live heater current-sense ADC, raw counts, averaged over 8 samples. |
| `/dev/buttons` | read | Names of buttons pressed since last read (e.g. `up down`), read-and-clear, **all buttons at once**. |
| `/dev/buttons/<name>` | read | One button only (`up`, `down`, `mute`, `manual`, `start`, `lamp`), read-and-clear, independent of the others. |
| `/dev/lamp` | read/write | Baby-light LED. `on`/`off`. |
| `/dev/relay` | write | The mechanical safety relay. `on`/`off`. Doesn't know or care who writes it — `alarmcheck` drives it automatically now, same as a human could. |
| `/dev/heater` | read/write | TPO/PWM heater power, `0`-`100`, or `on` (=100)/`off` (=0) aliases on write. Read returns the last commanded value. Drives real heat output. |
| `/dev/leds` | write | All 7 front-panel LEDs at once: `write "<name> on\|off"`, one of `aut warm low high fail chk man`. |
| `/dev/leds/<name>` | read/write | One LED only, independent of the other six. |
| `/dev/seg7big` | write | Large 7-segment display. Write a number, e.g. `36.5` — up to 3 digits, dot always lands after the middle digit (hardware limitation). |
| `/dev/seg7small` | write | Small 7-segment display. Same format. |
| `/dev/setpoint` | read/write | Temperature setpoint, °C, clamped `[30, 39]`. |
| `/dev/percent` | read/write | Manual heater power, `0`-`100`, clamped. The manual-mode counterpart to `/dev/setpoint`. |
| `/dev/heaterauto` | read/write | `on` = auto control, `off` = manual. Gates which source `follow` forwards to `/dev/heater`; `phase` forces `/dev/state` to `idle` whenever this isn't `on`. |
| `/dev/pid/kp` | read/write | PID proportional gain, %/°C. Default `3.0`. |
| `/dev/pid/ti` | read/write | PID integral time, seconds. Default `200.0`. Floored at `1.0` — it's a divisor in the formula. |
| `/dev/pid/td` | read/write | PID derivative time, seconds. Default `5.0`. |
| `/dev/pid/dt` | read/write | PID sample period, seconds. Default `1.0`. Floored at `0.1` — also a divisor. |
| `/dev/pid/integral` | read/write | Live integral term. Watch it while tuning; `echo 0 >` resets windup. |
| `/dev/pid/prevmeas` | read/write | Last measurement (derivative-on-measurement bookkeeping). Seeded by `phase` on each transition into `pid`. |
| `/dev/pidout` | read/write | PID's last computed output, `0`-`100`. |
| `/dev/state` | read/write | Phase machine's current phase: `idle`, `boost`, `coast`, `pid`, or `safe`. Written only by `phase`. |
| `/dev/autopower` | read/write | Whatever the active phase says heater power should be. What `follow` forwards to `/dev/heater` in auto mode. |
| `/dev/safepower` | read/write | Safe mode's lookup-table output, from `safelut`. |
| `/dev/alarm` | write | Buzzer + alarm LED, `on`/`muted`/`off`. `muted` keeps the LED lit, silences only the buzzer. Written by `alarm`, but nothing stops a human writing it directly. |
| `/dev/alarm/heater` | read/write | `on` = `alarmcheck` detected a current-sense/commanded-power mismatch. |
| `/dev/alarm/temphigh` | read/write | `on` = skin above 40°C (fixed testing threshold, `alarmcheck`). |
| `/dev/alarm/templow` | read/write | `on` = skin below 10°C (fixed testing threshold, `alarmcheck`). Not wired to the front-panel LEDs yet. |
| `/dev/tft/aut` | read/write | Mirrors `/dev/heaterauto` for the ST7735 display: `on` → gear icon. |
| `/dev/tft/man` | read/write | Mirrors `/dev/heaterauto` inverted: `on` → yellow hand icon. |
| `/dev/tft/chk` | read/write | `on` during `/dev/state`'s `safe` phase → sensor+warning icons blink together. |
| `/dev/tft/low` | read/write | Mirrors `/dev/alarm/templow` → blue cold face, FAIL+ALARM icons blink. |
| `/dev/tft/high` | read/write | Mirrors `/dev/alarm/temphigh` → red hot face, FAIL+ALARM icons blink. |
| `/dev/tft/fail` | read/write | Mirrors `/dev/alarm/heater` → heater-rod blinks red↔grey, FAIL+ALARM icons blink. |
| `/dev/tft/heater` | read/write | `0`-`100`, mirrors `/dev/heater` → drives the heater-rod's fill color + heat rays. |
| `/dev/tft/apgar` | read/write | Raw forward of `/dev/buttons/start`: `on` (re)starts the on-screen APGAR `MM:SS` clock at 0. The display owns the clock itself — this device just pulses. |
| `/dev/tft/mode` | read/write | `graphical` (default) or `text` — switches the whole screen between the warmer UI and a full-screen ASCII console. |
| `/dev/tft/seek` | write-only | Moves the text-console cursor to a row (`0`-`15`). Rejected (`-1`) unless `/dev/tft/mode` is `text`. |
| `/dev/tft/text` | read/write | Writes a line at the cursor row, then the cursor auto-advances — `echo Heater: 42 % > /dev/tft/text` just works. Rejected unless mode is `text`. |
| `/dev/tft/clear` | write-only | Wipes the text console and homes the cursor, without leaving text mode. Rejected unless mode is `text`. |

## Programs (`bin/...`, run from the shell as bare names)

| Program | Usage | What it does |
|---|---|---|
| `cat` | `cat [path]` | Read a device, or pass piped input through unchanged if no path given. |
| `echo` | `echo <words...>` | Print its arguments, space-joined. |
| `ls` | `ls` | List every device and program. |
| `thresh` | `thresh <threshold>` | Compare piped input to a threshold (literal number or a device path, read live), output `on`/`off`. `on` when input `>=` threshold. |
| `hyst` | `hyst <threshold> <band>` | Like `thresh`, but with a `<band>`-wide hysteresis dead zone — no chattering right at the threshold. **Heating polarity**: `on` when cold, `off` when warm — opposite of `thresh`. |
| `toggle` | `toggle <button-device> <target-device>` | If the button was pressed since last check, flip the target's `on`/`off` state. |
| `adjust` | `adjust <gate-device> <target-if-on> <target-if-off> <step>` | UP/DOWN steps whichever target the gate device currently selects. |
| `follow` | `follow <gate-device> <source-if-on> <source-if-off> <target>` | Every tick, copies whichever source the gate currently selects to target. The read-side twin of `adjust`'s shape. |
| `pid` | `pid <gate-device> <gate-value> <setpoint-device-or-literal>` | Measurement piped in. Steps once per `/dev/pid/dt` seconds, only while the gate device's reading matches `<gate-value>`; writes its output to `/dev/pidout`. |
| `monitor` | `monitor` | One status line: mode, phase, setpoint, skin temp, ambient, heater, current, pidout, percent, integral. Repeats its column header every 10 lines. Meant to run under `watch`, not `&`. |
| `phase` | `phase` | The temperature-phase transition engine. Self-paced ~1Hz; owns `/dev/state` as its only writer. See [Status](#status-what-actually-drives-the-heater-right-now). |
| `safelut` | `safelut` | Ambient temp piped in, looks up safe-mode's open-loop power from a hardcoded table, outputs it. Stateless, ungated. |
| `select` | `select <state-device> <label>=<source> [<label>=<source> ...]` | `follow`'s N-way sibling: outputs whichever labeled source matches the state device's current value. No match falls back to `0`. |
| `alarmcheck` | `alarmcheck` | Safety relay control + the three alarm conditions (heater/temphigh/templow), self-paced to ~15s. Drives `/dev/relay` and `/dev/alarm/*`. Ungated — runs the same in manual or auto mode. |
| `alarm` | `alarm <cond-device> [<cond-device> ...]` | ORs the given condition devices, drives `/dev/alarm`, honors the mute button. Runs at the normal ~150ms job cadence, deliberately not self-paced like `alarmcheck` — a mute press needs to register fast. |
| `ledwire` | `ledwire` | Drives all 7 front-panel LEDs from their fixed condition sources (mode, phase, alarm conditions), one job. |
| `tftwire` | `tftwire` | Drives the ST7735 display's `/dev/tft/*` from the same condition sources as `ledwire`, one job. The display's own redraw/blink rate is separate — a kernel task (`tftflush`), not this job. |

## Recipes

Poke at hardware directly:
```
cat /dev/skintemp
echo on > /dev/lamp
echo 25 > /dev/heater
```

A live thermostat with no dedicated control-loop code at all:
```
cat /dev/skintemp | hyst /dev/setpoint 1.0 > /dev/heater &
```

A high-skin-temp warning lamp that tracks a live, adjustable setpoint:
```
cat /dev/skintemp | thresh /dev/setpoint > /dev/lamp &
```

Make the physical buttons do things:
```
toggle /dev/buttons/lamp /dev/lamp &                 # LAMP button toggles the baby-light
toggle /dev/buttons/manual /dev/heaterauto &          # MANUAL button toggles auto/manual mode
adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &   # UP/DOWN adjusts whichever the mode selects
```

Full heater control, manual and auto, boost/coast/PID/safe-mode phases
included, plus the safety relay/alarm checks, the front-panel LEDs, and
the display/button UI jobs — `follow`'s own line never changes shape
no matter how much richer the auto side gets, since everything
upstream funnels into one `/dev/autopower`. This exact recipe is baked
in as the `boot` script (see [Using the shell](#using-the-shell)) and
runs automatically at every power-up — `run boot` also does all
thirteen lines at once on demand, e.g. after a `kill` of everything
mid-session:
```
follow /dev/heaterauto /dev/setpoint /dev/percent /dev/seg7small &
toggle /dev/buttons/manual /dev/heaterauto &
adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &
phase &
select /dev/state boost=80 coast=0 pid=/dev/pidout safe=/dev/safepower > /dev/autopower &
cat /dev/skintemp | pid /dev/state pid /dev/setpoint > /dev/pidout &
cat /dev/ambient | safelut > /dev/safepower &
follow /dev/heaterauto /dev/autopower /dev/percent /dev/heater &
cat /dev/skintemp > /dev/seg7big &
alarmcheck &
alarm /dev/alarm/heater /dev/alarm/temphigh /dev/alarm/templow &
ledwire &
tftwire &
```

Manage what's running:
```
jobs
kill %1
```

Pause without wasting CPU (everything else keeps running):
```
sleep 3000
```

Record and replay a sequence of commands (RAM-only, lost on reboot):
```
script demo
> echo on > /dev/leds/chk
> sleep 1000
> echo off > /dev/leds/chk
> .
run demo
```

Watch the warmer's status once a second, Ctrl-C to stop:
```
watch 1000 monitor
```

Or watch any single value directly, without a dedicated program:
```
watch 500 cat /dev/skintemp
```

## Status: what actually drives the heater right now

`follow` is the sole writer to `/dev/heater`: `/dev/autopower` while
`/dev/heaterauto` is `on`, `/dev/percent` while it's `off`. Manual mode
is unchanged. Auto mode now has real phases, recovered from
babywarmer's own `task_pidctrl()` state machine:

- **boost** — 80% power until skin nears setpoint, with a 5-minute
  no-rise watchdog (→ `safe` if skin isn't actually climbing).
- **coast** — 0% power for 45s, letting the current-sense average
  settle, then → `pid` with a seeded integral (babywarmer's own
  documented head-start).
- **pid** — `prog/pid.cpp` does the control math, gated on
  `/dev/state` reading `pid`. `phase` runs two watchdogs alongside it
  (fast sustained drop, slow sustained low) — either → `safe`.
- **safe** — `safelut`'s ambient-temp lookup table drives the heater
  open-loop, capped at 55%. Recovers to `pid` once skin gets back
  within 2°C of setpoint.

`phase` is `/dev/state`'s only writer, self-paced at ~1Hz like `pid`.
`select` is the only thing standing between the four phases and
`/dev/autopower` — `follow`'s own line at the top never has to change
shape no matter how much richer the auto side gets.

`alarmcheck` is back too — babywarmer's own `heater_check_task`,
renamed once it grew two more conditions, self-paced to ~15s (same
pattern as `pid`/`phase`, just a longer interval), driving `/dev/relay`
automatically and reporting `/dev/alarm/{heater,temphigh,templow}`
(temphigh/templow use Leon's own fixed testing thresholds, 40°C/10°C,
not babywarmer's original relative-to-setpoint/relative-to-ambient
margins). It's a separate, orthogonal safety subsystem, not part of
the temperature-phase machine above — runs unconditionally in either
manual or auto mode.

`alarm` reacts to those three conditions — babywarmer's own
`task_alarm`, split into its own program running at the normal ~150ms
job cadence (not self-paced like `alarmcheck` — a mute press needs to
register fast). ORs whichever condition devices it's given, drives
`/dev/alarm`'s buzzer+LED, and honors the MUTE button with the same
60-second snooze babywarmer used (LED stays lit while muted; only the
buzzer is silenced).

`ledwire` ties it all to the front panel — one job, seven fixed
mappings (Leon's own definitions): `aut`/`man` mirror `/dev/heaterauto`,
`warm`/`chk` light during `/dev/state`'s `boost`/`safe` phases,
`low`/`high`/`fail` mirror `/dev/alarm/{templow,temphigh,heater}`.
Bundled deliberately, unlike everywhere else in this codebase that
favors composing small independent pieces — these seven mappings are
fixed, known wiring, not something to mix and match, same call
`monitor` already made for reading many devices into one status line.

`tftwire` is `ledwire`'s sibling for a second front panel: a 1.8"
ST7735 LCD, additive alongside the LEDs (nothing removed), driven from
the same source devices minus `warm` (redundant with the heater-rod's
own color on screen) plus a new `heater` (0-100, a continuous value
LEDs couldn't show). The display itself lives entirely behind a
vendored blackbox driver (`tft/`, copied from the separate `st7735`
repo — see `tft/README.md`); `dev/tft.cpp` and `tftwire` only ever
touch its `display_init()`/`display_update()` entry points. Redraw and
blink animation run off a dedicated kernel task (`tftflush`, 20ms), not
a job — the display keeps animating even if `tftwire` gets `kill`ed,
it just stops picking up new state.

Added 2026-08-28: an on-screen APGAR elapsed-time clock (`MM:SS`,
flashes yellow at the real 1/5/10-minute checkpoints), owned entirely
by the vendored display driver — picoos doesn't track time for this at
all. `/dev/tft/apgar` is a raw forward of `/dev/buttons/start` (via
`tftwire`); a press (re)starts the clock at 0, detected as a rising
edge inside `display.c` itself.

Added 2026-08-29: a second screen mode, a full-screen 21x16 ASCII text
console (`/dev/tft/mode`, `text` vs. `graphical`), driven by a small
seek/write/clear command set that mirrors real file I/O — `seek`
positions the cursor (`lseek()`), `write` lands a line there and
auto-advances the cursor (a real file's write-advances-the-offset
behavior), `clear` wipes the screen and homes the cursor. `bin/echo`
already works against it unmodified, via the shell's existing `>`
redirect: `echo Heater: 42 % > /dev/tft/text`. All four devices are
purely mode-gated, not condition-mirrored — nothing in `tftwire` drives
them, they're meant to be typed or scripted directly (e.g. a future
debug/status job). Writing to `seek`/`text`/`clear` while mode is
`graphical` is rejected (`-1`) rather than silently ignored, even
though `display.c` already no-ops text commands internally while
graphical is on screen. The graphical UI itself also picked up two
small additions from the same re-sync: a second (horizontal) divider
under the face, and two small permanent label icons (thermometer next
to the face, clock next to the APGAR timer) disambiguating "this row is
about temperature" from "this row is a clock reading" — no state-struct
changes, pure layout.

Still deliberately not brought back (see `prog/phase.cpp`'s header
comment for the reasoning): the setpoint-jump-triggers-reboost path.

## Architecture, briefly

- `kernel/task.h` — the cooperative scheduler. `task_register(name, fn, period_ms)`, `task_run()`.
- `kernel/fs.h` — the device namespace. `device_t{name, open, close, read, write}`, one flat table.
- `kernel/prog.h` — the program registry, same shape as `kernel/fs.h` but for text-in/text-out filters instead of hardware.
- `jobs.h`/`jobs.cpp` — pipeline execution (`|`/`<`/`>`) and background job control (`&`/`jobs`/`kill`), built entirely on top of `kernel/task.h` — no scheduler changes needed even for `sleep`/button-driven jobs.
- `shell.cpp` — `task_shell`, the interactive `%` prompt.
- `dev/*.cpp`, `prog/*.cpp` — one file per device/program, each self-contained.
- `tft/` — vendored ST7735 driver (blackbox, unmodified copy of the separate `st7735` repo). `dev/tft.cpp` is the only picoos file that includes it.

Known, documented limitations live in the source comments where they're relevant (e.g. `hyst`/`follow`'s single shared state if run twice concurrently, `JOB_POLL_MS`'s one fixed interval for all background jobs) — grep for "not solved" if curious.

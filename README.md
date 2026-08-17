# picoos

An experimental, Plan 9-flavored little OS for the RP2040, built by
gutting a working infant-warmer control loop (see
[babywarmer](https://github.com/leonhiem/babywarmer), the stable
sibling this was forked from) and replacing it with: a cooperative
task scheduler, a device namespace where hardware is just files, an
interactive shell, and pipes to wire small programs and devices
together.

No PID controller. No `ioctl()`. Everything is a file: `open`,
`close`, `read`, `write`. Small pieces, composed from the command line.

## Philosophy

- **Everything is a file.** Hardware (`/dev/skintemp`, `/dev/lamp`,
  `/dev/heater`, ...) and small programs (`bin/cat`, `bin/thresh`,
  ...) are both just named, `open`able things — `ls` lists them
  together, in one namespace.
- **The namespace is the wiring.** There's no PID loop and no plan to
  hardcode one. Control behavior is built by piping small pieces
  together from the shell, e.g. a live thermostat is
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
picoos -- type 'ls' to see /dev and bin/, 'jobs'/'kill' to manage background pipelines
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

Backspace works. Unknown commands, bad paths, and rejected writes
print a short error instead of doing something silently wrong.

## Devices (`/dev/...`)

| Device | R/W | What it does |
|---|---|---|
| `/dev/skintemp` | read | Live skin-temperature reading (ADS1115/NTC), °C. Fresh value every read. |
| `/dev/current` | read | Live heater current-sense ADC, raw counts, averaged over 8 samples. |
| `/dev/buttons` | read | Names of buttons pressed since last read (e.g. `up down`), read-and-clear, **all buttons at once**. |
| `/dev/buttons/<name>` | read | One button only (`up`, `down`, `mute`, `manual`, `start`, `lamp`), read-and-clear, independent of the others. |
| `/dev/lamp` | read/write | Baby-light LED. `on`/`off`. |
| `/dev/alarm` | write | Buzzer + alarm LED together. `on`/`off`. |
| `/dev/relay` | write | The mechanical safety relay. `on`/`off`. Bare manual toggle, no automatic tripping. |
| `/dev/heater` | write | TPO/PWM heater power, `0`-`100`, or `on` (=100)/`off` (=0) aliases. Drives real heat output. |
| `/dev/leds` | write | All 7 front-panel LEDs at once: `write "<name> on\|off"`, one of `aut warm low high fail chk man`. |
| `/dev/leds/<name>` | read/write | One LED only, independent of the other six. |
| `/dev/seg7big` | write | Large 7-segment display. Write a number, e.g. `36.5` — up to 3 digits, dot always lands after the middle digit (hardware limitation). |
| `/dev/seg7small` | write | Small 7-segment display. Same format. |
| `/dev/setpoint` | read/write | Temperature setpoint, °C, clamped `[30, 39]`. |
| `/dev/percent` | read/write | Manual heater power, `0`-`100`, clamped. The manual-mode counterpart to `/dev/setpoint`. |
| `/dev/heaterauto` | read/write | `on` = auto/PID control, `off` = manual. Only stores the mode right now — see [Status](#status-what-actually-drives-the-heater-right-now). |

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
| `follow` | `follow <gate-device> <on\|off> <source> <target>` | While the gate matches, copy source's value to target every tick; otherwise hold target at `0`. |

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
follow /dev/heaterauto off /dev/percent /dev/heater &     # manual mode's /dev/percent actually drives the heater
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

## Status: what actually drives the heater right now

`/dev/heaterauto` only stores and reports auto/manual mode. `follow`
(above) wires manual mode's `/dev/percent` to the real heater. Auto
mode doesn't drive anything yet — no PID loop exists, and building one
is explicitly deferred ("for another day"). The natural next program
would reuse the exact same `follow` shape once a real auto-mode source
exists:

```
follow /dev/heaterauto on <auto-source> /dev/heater &
```

## Architecture, briefly

- `kernel/task.h` — the cooperative scheduler. `task_register(name, fn, period_ms)`, `task_run()`.
- `kernel/fs.h` — the device namespace. `device_t{name, open, close, read, write}`, one flat table.
- `kernel/prog.h` — the program registry, same shape as `kernel/fs.h` but for text-in/text-out filters instead of hardware.
- `jobs.h`/`jobs.cpp` — pipeline execution (`|`/`<`/`>`) and background job control (`&`/`jobs`/`kill`), built entirely on top of `kernel/task.h` — no scheduler changes needed even for `sleep`/button-driven jobs.
- `shell.cpp` — `task_shell`, the interactive `%` prompt.
- `dev/*.cpp`, `prog/*.cpp` — one file per device/program, each self-contained.

Known, documented limitations live in the source comments where they're relevant (e.g. `hyst`/`follow`'s single shared state if run twice concurrently, `JOB_POLL_MS`'s one fixed interval for all background jobs) — grep for "not solved" if curious.

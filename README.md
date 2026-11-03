# mpdcc
A compiler from MPD, the notation of Andrews' slides on RPC and rendezvous, to Ada.
Each process becomes an Ada task, so an exercise in RPC or rendezvous can be checked
by running it.

The notation comes from G. R. Andrews, *Foundations of Multithreaded, Parallel, and
Distributed Programming* (2000), whose language MPD follows SR (Synchronizing
Resources). mpdcc accepts the slides' spelling of it, such as `Module`, `Body`,
`Process deposit(item)` and `End BoundedBuffer;`.

# Example
```
# bounded_buffer.mpd

const int n = 5;
const int ITEMS = 20;
type elem = int;

Module BoundedBuffer
    op deposit(elem);
    op fetch(result elem);
Body
    Process Buffer {
        elem buf[n]; int front = 0, rear = 0, count = 0;
        while (true) {
            in deposit(item) and count < n ->
                    buf[rear] = item; count++; rear = (rear + 1) % n;
            [] fetch(item) and count > 0 ->
                    item = buf[front]; count--; front = (front + 1) % n;
            ni
        }
    }
End BoundedBuffer;

process Producer {
    for [i = 1 to ITEMS] {
        call BoundedBuffer.deposit(i);
    }
}

process Consumer {
    for [i = 1 to ITEMS] {
        elem item;
        call BoundedBuffer.fetch(item);
        write("fetched", item);
    }
}
```

```sh
make                                         # builds build/mpdcc
build/mpdcc examples/bounded_buffer.mpd      # writes bounded_buffer.adb and mpd_runtime.ad[sb]
gnatmake bounded_buffer.adb                  # builds ./bounded_buffer
./bounded_buffer --trace                     # runs it, printing every call and who serves it
```

# Building
You need `g++` (C++20) and `make`; running a translated program needs GNAT,
and the tests need Catch2 3.

```sh
make            # build/mpdcc, with the objects in build/objs
make test       # the Catch2 tests in tests/, which also build and run MPD programs
make examples   # each examples/NAME.mpd built into build/examples/NAME
make clean      # removes build/
```

# Options
```
mpdcc [options] INPUT
  -o OUTPUT         the Ada main procedure, a lower-case name ending in .adb
  -D NAME=VALUE     define a constant, or override one
  -fno-runtime      don't write mpd_runtime.ads and mpd_runtime.adb
  -fdump-tokens     print the tokens and stop
  -w, -Werror       no warnings, or warnings as errors
```

A translated program takes `--trace` (print every invocation and who serves it),
`--quiet` (don't print hooks), `--delay=MS` (the longest pause of a hook, default 10),
`--seed=N` and `--timeout=S` (stop after S seconds). It exits with 0 when every
process has ended, or when the only ones left wait in `in` or `receive`, as servers
do. It exits with 1 when a process fails, such as on an index out of range, and with
3 on a deadlock, when a process waits in `call` or `P` and nothing can run; it then
lists what each process waits for.

# The language
| MPD | Meaning |
| --- | --- |
| `module M op ...; body ... end M;` | A module: its ops, then its variables, procs and processes; `Module`, `Body` and `End` too |
| `op deposit(int);`, `op fetch(res int x);`, `op size() returns int;` | An op; formals are `val` (the default), `var` or `res` (`result`) |
| `proc deposit(item) { ... }`, `Process deposit(item) { ... }` | A proc serves an op: each invocation runs its body |
| `process Buffer { ... }`, `process Client[i = 1 to N] { ... }` | Processes, which start with the program |
| `call M.deposit(x);`, `x = M.size() + 1;` | Invoke an op and wait; `M.` is needed outside the module |
| `send M.deposit(x);` | Invoke an op without waiting; to a proc, this starts a new process |
| `in op1(a) and B by e -> ... [] op2(b) returns r -> ... ni` | Rendezvous: serve the oldest invocation whose guard `B` holds, or with `by`, the one with the smallest `e`; `B` may use the formals |
| `receive op(x, y);` | Take the oldest invocation of `op`, as `in op(a, b) -> x = a; y = b; ni` |
| `?op` | The number of invocations of `op` waiting |
| `const int N = 5;`, `type elem = int;` | Constants, which `-D` overrides, and type names |
| `int x = 0, a[N];`, `bool`, `char`, `real` | Variables and arrays, indexed from 0 |
| `sem s = 1, fork[N] = 1;`, `P(s)`, `V(s)`, `wait(s)`, `signal(s)` | Semaphores |
| `if`, `while`, `for [i = 1 to n]`, `for [i = 1 to n st i != j]`, `{ ... }` | Statements as in C and on the slides |
| `=`, `+=`, `-=`, `*=`, `++`, `--`, `+ - * / %`, `== != < <= > >=`, `&& \|\| !` | Operators as in C |
| `write(x, "text")`, `random(n)`, `nap(ms)` | Print values, a random number from 0 to n - 1, a pause |
| `work(i);`, `THINK;` | A hook, a call nothing defines: it prints the process and the call, then pauses |
| `#`, `//`, `/* ... */` | Comments |

A call of a proc runs the proc's body in the calling process, which waits for it
anyway; a send to a proc starts a new process, named after the op, such as `work#2`.

# License
GPLv3 or later; see [LICENSE.md](LICENSE.md).

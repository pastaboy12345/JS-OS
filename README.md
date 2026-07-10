# JS-OS

JS-OS is a freestanding x86_64 kernel with QuickJS as its userspace. It boots
to a framebuffer TTY and accepts keyboard input directly from QEMU's PS/2
keyboard device.

## Run

Install an `x86_64-elf-gcc` cross compiler, QEMU, and xorriso, then run:

```sh
make run
```

Click the QEMU window to capture the keyboard. The shell accepts familiar
commands such as `help`, `clear`, `uname -a`, `free`, `uptime`, and `date`.
Anything that is not a built-in command is evaluated as JavaScript:

```js
1 + 2
Kernel.memory.stats()
Kernel.graphics.info()
Math.sin(Math.PI / 2)
```

The boot environment and shell commands are ordinary JavaScript in
`src/js/boot.js`. Native kernel bindings are installed as the global `Kernel`
object by `src/kernel/api.c`.

Graphics, CSS colors, Canvas-style drawing, native LiteHTML/CSS rendering,
FreeType text, and React-like components are documented in
[`GUIDEBOOK.md`](GUIDEBOOK.md).

COM1 mirrors terminal output for debugging, but interactive input comes from
the QEMU window rather than the serial console.

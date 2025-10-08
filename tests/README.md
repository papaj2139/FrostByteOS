# FrostByteOS Test Programs

each binary in this directory is a standalone test meant to run inside the FrostByteOS QEMU environment. After execution, it prints a single PASS/FAIL line to the TTY

- `test_memory`
  - Scenario: Grow and shrink the heap using `sbrk()`/`brk()`, then touch the allocated pages.
  - Expected output: `TEST memory: PASS`

- `test_process`
  - Scenario: Basic `fork()`/`waitpid()` lifecycle with explicit exit status handling.
  - Expected output: `TEST process: PASS`

- `test_ipc`
  - Scenario: Pipe roundtrip between parent/child plus `dup2()` redirection into a file.
  - Expected output: `TEST ipc: PASS`

- `test_vfs`
  - Scenario: Create/remove directories and files, verify metadata with `stat()`.
  - Expected output: `TEST vfs: PASS`


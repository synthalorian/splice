# splice

> Minimalist Git alternative for binary assets. Content-addressable storage, delta compression, designed for game devs drowning in .psd and .fbx files.

**Language:** C  
**Constraint:** Frankenstein week  
**Stack:** libgit2-inspired, xxhash, zstd, mmap

---

## Features

- Content-addressable storage (xxhash)
- Delta compression for binary files (zstd)
- Lazy checkout — materialize files on demand
- Large file support (mmap, streaming)
- Partial clone / sparse checkout
- CLI: init, add, commit, checkout, log, diff
- C library + CLI binary

---

## Development Plan

1. Phase 1: Object store — content-addressable blob storage
2. Phase 2: Delta compression engine (zstd)
3. Phase 3: Tree object and commit graph
4. Phase 4: CLI: init, add, commit
5. Phase 5: Checkout with lazy materialization
6. Phase 6: Partial clone and sparse checkout
7. Phase 7: diff and log commands
8. Phase 8: C library API and bindings

---

## Getting Started

### Prerequisites

- C toolchain

### Build

```bash
# See PLAN.md for detailed build instructions per phase
cd splice
```

### Run

```bash
# See PLAN.md for run instructions
```

---

## Architecture

See `PLAN.md` for detailed architecture decisions and implementation notes.

---

## License

MIT

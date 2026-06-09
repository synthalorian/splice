# splice — Implementation Plan

## Project Overview

Minimalist Git alternative for binary assets. Content-addressable storage, delta compression, designed for game devs drowning in .psd and .fbx files.

**Language:** C  
**Constraint:** Frankenstein week  
**Stack:** libgit2-inspired, xxhash, zstd, mmap

---

## Phase Breakdown

### Phase 1: Object store — content-addressable blob storage

**Goal:** Phase 1: Object store — content-addressable blob storage

**Deliverables:**
- [x] Core implementation
- [x] Tests
- [x] Documentation update

**Notes:**
- Object store implemented with xxhash64 content-addressing, sharded storage layout (objects/xx/xxxx...), atomic writes via temp+rename, and forward-compatible binary object header (type + 8-byte BE size).

---

### Phase 2: Delta compression engine (zstd)

**Goal:** Phase 2: Delta compression engine (zstd)

**Deliverables:**
- [x] Core implementation
- [x] Tests
- [ ] Documentation update

**Notes:**
- Delta compression implemented using zstd dictionary compression. Base object is used as dictionary to compress new data. Delta objects store base OID reference + zstd compressed payload. `splice_delta_create()` and `splice_delta_apply()` provide high-level API.

---

### Phase 3: Tree object and commit graph

**Goal:** Phase 3: Tree object and commit graph

**Deliverables:**
- [x] Core implementation
- [x] Tests
- [ ] Documentation update

**Notes:**
- Tree objects implemented with binary serialization (count + sorted entries with mode/name_len/name/oid).
- Commit objects use text format (tree/parent/author/time headers + message).
- Refs stored as files under refs/ directory; HEAD is symbolic ref.
- Bug fixes: empty tree support, empty message roundtrip, HEAD parsing off-by-one. 

---

### Phase 4: CLI: init, add, commit

**Goal:** Phase 4: CLI: init, add, commit

**Deliverables:**
- [x] Core implementation
- [x] Tests
- [x] Documentation update

**Notes:**
- CLI implemented with `init`, `add`, `commit` subcommands.
- Index/staging area added as binary file `.splice/index` with versioned format.
- `init` creates `.splice/` directory with `objects/`, `refs/`, and `HEAD` symbolic ref.
- `add` reads files, stores as blobs, and adds to index with mode detection (0644/0755).
- `commit` builds tree from index, creates commit object with parent chain, updates current ref, and clears index.
- Repository discovery walks up from current directory looking for `.splice/`.

---

### Phase 5: Checkout with lazy materialization

**Goal:** Phase 5: Checkout with lazy materialization

**Deliverables:**
- [ ] Core implementation
- [ ] Tests
- [ ] Documentation update

**Notes:**
- 

---

### Phase 6: Partial clone and sparse checkout

**Goal:** Phase 6: Partial clone and sparse checkout

**Deliverables:**
- [x] Core implementation
- [x] Tests
- [x] Documentation update

**Notes:**
- Sparse checkout implemented with shell-style wildcard pattern matching (fnmatch), pattern persistence in `.splice/sparse-checkout`, and negation support (`!pattern`).
- `splice_checkout_sparse()` filters tree entries by patterns before writing to working directory.
- CLI `sparse-checkout` subcommand supports `set`, `add`, `remove`, and `list` operations.
- Partial clone infrastructure: `splice_object_promise()` marks objects as referenced-but-missing, `splice_object_is_promised()` checks promise status, `splice_object_is_local()` wraps existence check for partial-clone semantics.

---

### Phase 7: diff and log commands

**Goal:** Phase 7: diff and log commands

**Deliverables:**
- [ ] Core implementation
- [ ] Tests
- [ ] Documentation update

**Notes:**
- 

---

### Phase 8: C library API and bindings

**Goal:** Phase 8: C library API and bindings

**Deliverables:**
- [ ] Core implementation
- [ ] Tests
- [ ] Documentation update

**Notes:**
- 

---

## Architecture Notes

### Key Decisions

- 

### Data Flow

```
[Input] → [Parse] → [Transform] → [Output]
```

### Error Handling Strategy

- 

---

## Testing Strategy

- Unit tests for core functions
- Integration tests for full pipeline
- Benchmarks for performance-critical paths

---

## Open Questions

1. 
2. 

---

*Generated for opencode sprint. Implement phase by phase. DO NOT RESEARCH. Build directly.*

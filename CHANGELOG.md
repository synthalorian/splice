# Changelog

All notable changes to this project will be documented in this file.

## [1.0.0] - 2025-06-10

### Added
- Content-addressable blob storage using xxhash64
- Delta compression engine with zstd for binary files
- Tree objects for directory structure representation
- Commit graph with parent chains
- Refs and symbolic HEAD support
- Index / staging area for preparing commits
- CLI commands: init, add, commit, checkout, log, diff
- Lazy checkout with placeholder files and materialization
- Sparse checkout with pattern matching and negation
- Partial clone support with promised objects
- C library API (libsplice.a / libsplice.so)
- Comprehensive test suite covering all 8 phases

### Technical Details
- **Language:** C11
- **Dependencies:** libzstd, libxxhash
- **Build:** Makefile with static and shared library targets
- **Tests:** 69 tests across 10 test modules

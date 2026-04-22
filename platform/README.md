Platform scaffolding for xv6 ports lives here.

Layout:
- common/: shared platform-facing headers and helpers.
- nemu/: NEMU-specific build and launch glue.

Build defaults:
- `PLATFORM` is required for normal builds.
- Run `make platform` to list supported platform values.
- Each supported platform must provide `platform/<name>/platform.mk`.

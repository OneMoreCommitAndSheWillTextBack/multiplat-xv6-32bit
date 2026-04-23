QEMU-specific build and launch glue lives here.

Use `platform.mk` in this directory for:
- platform-only source lists
- platform-specific compiler flags
- QEMU launch and GDB targets and overrides

Notes:
- `PLATFORM=qemu` uses the virtio block driver path.
- The launch targets attach `fs.img` as a virtio-mmio disk on QEMU `virt`.
- QEMU is started with `virtio-mmio.force-legacy=false` so the kernel sees
  the modern MMIO transport expected by `kernel/virtio_disk.c`.

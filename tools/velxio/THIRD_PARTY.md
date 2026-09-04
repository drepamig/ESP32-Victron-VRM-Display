# Velxio adapter provenance

The worker is loaded from the pinned Velxio Docker image at execution time and
checked against `simulation/velxio/runtime-lock.json`. `worker.py` adds the
FT6206 proxy, instruction counting, guest-clock/display barriers, and clean
flash retention. It does not modify the cached upstream image.

`ili9341.mts` is extracted from `frontend/src/simulation/parts/ComplexParts.ts`
at Velxio revision `77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0`, with the measured
RGB565 inversion and RAMWR cursor corrections plus a module export. The source
hash is recorded in the lock. The headless adapter applies the diagram's panel
rotation; it does not recolor or resize output.

Upstream: https://github.com/davidmonterocrespo24/velxio/tree/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0

Velxio is licensed under AGPL-3.0; its license is retained as `LICENSE.velxio`.
These files retain that license for the adapted upstream material. No upstream
web service or first-start installer is launched by the local runner.

The maintained serial boundary also accepts the repository's dummy `SIM venus`
address fixtures. This does not change the pinned runtime, DIO configuration,
timing, touch, or display adapters.

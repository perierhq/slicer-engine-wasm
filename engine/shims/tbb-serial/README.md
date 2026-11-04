# Serial oneTBB shim (single-threaded engine only)

Header-only stand-in for oneTBB, used by the `st` engine variant, where there are no threads. It
covers exactly the TBB API that Orca's `src/libslic3r` and the bundled `deps_src` libraries use
(checked with `grep -rhoE "\b(oneapi::)?tbb::[A-Za-z_0-9:]+"` and a grep of the `tbb/` includes):
`blocked_range`, `blocked_range2d`, `parallel_for`, `parallel_reduce`, `parallel_for_each`,
`parallel_pipeline` / `make_filter` / `flow_control` / `filter_mode`, `task_group`,
`this_task_arena`, `task_arena`, `global_control`, `spin_mutex`, `concurrent_vector`,
`concurrent_unordered_map`, `concurrent_unordered_set`, `scalable_allocator` and the partitioners.

Every algorithm runs its body on the calling thread, in order, which is one of the schedules real
TBB is allowed to pick, so results are the ones TBB could produce. Things worth knowing:

- `tbb/version.h` reports `TBB_VERSION_MAJOR 2021`. Orca picks the oneTBB API
  (`tbb::filter_mode`, `tbb::global_control`) from that macro (GCode.cpp, utils.cpp); an older
  number would pull in `tbb/pipeline.h` and `tbb/task_scheduler_init.h`, which do not exist here.
- `this_task_arena::max_concurrency()` is 1. `Slic3r::name_tbb_thread_pool_threads_set_locale()`
  (Thread.cpp) blocks until that many tasks run at once, so any other value would hang.
- `scalable_allocator<T>` is a distinct allocator type (plain `operator new`), not an alias of
  `std::allocator`, so `Slic3r::Points` stays a different type from `std::vector<Point>`, as with
  real TBB.
- `concurrent_vector` keeps element addresses stable on growth (it is built on `std::deque`), and
  `push_back`/`emplace_back`/`grow_by` return iterators, as in oneTBB.
- `task_group` runs each task immediately; an exception is kept and rethrown by `wait()`, as TBB
  does.
- The layout mirrors oneTBB: the definitions live in `oneapi/tbb/*.h` inside `oneapi::tbb`,
  `tbb` is a namespace alias of it, and `tbb/*.h` forward to `oneapi/tbb/*.h`.

The multi-threaded variant (`mt`) uses real oneTBB 2021.12 from the dependency prefix instead.

Derived from the serial shim of Hiosdra/OrcaWasm (`wasm/shims/{tbb,oneapi}`, AGPL-3.0,
© Oskar Drozda and contributors); rewritten and extended for Muon3D, 2026.

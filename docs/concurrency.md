<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Modified in smlib-rs, not part of an NVIDIA release: from usd-brep's .agents/operations/concurrency.md. -->

<a name="context--concurrency"></a>

# Context and Concurrency

**Source:** `SmApiGeneral.h` (`SmApiCreateContext`, `SmApiGetOrCreateContext`), `source/SMLib/inc/SmContext.h`, `source/SMLib/inc/SmConfig.h`, `source/SMLib/inc/SmCacheMgr.h`

## Rules (smlib-rs)

- **Threads may run `SmApi*` operations at the same time**, each on objects of its own. The
  default `SmContext` (`SmApiGetOrCreateContext()`) is per thread, and so is every global SMLib
  writes during an operation; `tools/threads/allowed.txt` lists the writable globals that stay
  process-wide and why they are safe. Checked by `tools/threads` (a harness that compares
  every operation in 1 to 8 threads with a serial run, under ThreadSanitizer, and a check of the
  libraries' writable globals).
- **An object belongs to the thread whose context built it.** Another thread may use it, delete
  it or take it as a boolean tool only while the building thread runs no `SmApi*` call. Copies of an
  object several threads read (`SmApiBrepCopy`) are taken under a lock of that object. Several
  threads must not read the same object at once (wrong results were measured).
- **A thread that ends calls `SmApiDeleteContext()`** after every object it built is deleted. Every
  object keeps a pointer to the context that built it (`SmObject::m_cpContext`), so a thread whose
  objects live on in other threads keeps its context until they are gone. The next `SmApi*` call
  in that thread would create a new context.
- **A running operation cannot be cancelled**, in a thread or in a process: the escape callback
  (`smos_SetExcapeCallback`) is not asked by any operation the `smlib` crate calls. A crash in an operation
  ends the whole process, so a host that runs operations in threads keeps a process it can restart.
- **Release builds only.** Debug builds (`SM_DEBUG_CODE`) keep debug counters in globals that
  threads share.
- **Operations run single-threaded** in the shipped build: one call runs on one thread; throughput
  comes from several threads or processes.
- **Keep one long-lived default `SmContext` per worker thread** across the edit loop. In the
  shipped build, caches belong to objects; keeping the context alive alone does not preserve caches
  when those objects are replaced.

## What was shared before (NVIDIA's build)

NVIDIA's `SM_API` layer has one process-global default `SmContext`. It holds unlocked mutable
state (traversal mark counters `m_lCurrentMark*`, mark locks, `m_bDoingBoolean`), and NLib keeps
the state of some routines in globals (spirals, `NL_MTOL`, `NL_ITLIM`, `NL_ERROR`, fitting error
lists, a recursion guard in the surface intersector). Threads sharing them gave wrong results from
2 threads and crashes from 4; ThreadSanitizer reports the races. smlib-rs makes all of them
per thread (`thread_local`).

## Operations are single-threaded

Individual operations do not use internal thread parallelism in the shipped build:
a single call (tessellation included) runs on one thread. Some parallel code paths
exist in the source but are gated behind build options that are not enabled, so
they are compiled out. Scale throughput by running more worker processes, each on
its own context.

## Cache lifetime

In the shipped build, `SM_USE_GLOBAL_CACHE` is disabled. Object caches are stored
on individual `SmAObject` instances; their lifetime and reuse depend on those
objects and cache invalidation. Keeping the context alive alone does not retain
caches for objects that are deleted or replaced, and edits may invalidate caches
even when an object survives. The same distinction applies in a browser/WASM worker.

When the optional `SM_USE_GLOBAL_CACHE` configuration is enabled, the `SmContext`
instead owns a `SmGlobalCache` containing curve, surface, trimmed-surface, and BRep
cache queues. That configuration exposes cache-size parameters on the context
constructor. Destroying the context discards those queues; a long-lived context
retains them subject to cache invalidation and eviction. This optional shared cache
also has unlocked mutable state and must not be treated as thread-safe; it is per context, so
per thread.

## Startup

`SmApiGetOrCreateContext()` creates the calling thread's context on its first use; the `smlib`
crate's `thread::start()` does so explicitly.

## WASM workers

A browser/WASM worker counts as a separate worker only when it runs its own
independently instantiated module with its own, unshared `WebAssembly.Memory`. It
then has its own global context, and the one-context-per-worker rule applies.
Emscripten pthreads and shared-memory Wasm Workers share the module's linear
memory, and with it ordinary globals such as the default context. Treat them as
threads: do not make concurrent `SmApi*` calls from them.


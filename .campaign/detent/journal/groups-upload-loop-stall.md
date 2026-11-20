# groups-upload-loop-stall: journal

State: waiting on jobs 261119-200155-ebcce0 (probe 4, run 44671627878), 261119-200157-067f2c (CI 44671575494 at d7b49a1), 261119-200137-1dd882 (suite); then set "increment ready: --upto d7b49a1" if the probe log shows raw offsets, build, UUID, slide
Quest: .campaign/detent/quests/groups-upload-loop-stall.md (the lead's; never edit it)

## Doing

The lead's ruling on kernel frames (raw, no nearest-export names): done at consensus:d7b49a1
(quest branch, pushed). The spindump report's kernel frames print as `kernel + 0xN [addr]`, under
`upload: kernel build <sw_vers> uuid <loaded kernel's> base <slid base> slide <base - on-disk
__TEXT vmaddr> unslid __TEXT 0x...` (base and UUID from the report's Binary Images kernel line;
the vmaddr from `otool -l`). Offline symbolication against the matching KDK is stated in
.github/workflows/ci.yml's header and docs/mechanisms/host-transport.md (Checks); CI downloads none.
Formatting checked against probe 3's artifact with a standalone harness (kernel + 0x1300a0 for
kernel + 1245344, slide 0xe8000).

Proof: probe 4, run 44671627878 (consensus:596df12 on branch probe/groups-upload-loop-stall-raw,
d7b49a1 plus the forced 1.3 s hold; never lands, one shard, hold only): its log must show raw
offsets, build, UUID, slide. Suite 261119-200137-1dd882 and CI 44671575494 at d7b49a1.

## Next

1. All three green and probe 4's log has the four items: State "increment ready: --upto d7b49a1",
   A2 stays open (no CI failure with stacks yet).

## Blocked on

Nothing.

## Notes

- A1. Five probed CI failures of groups_upload, all macos-x86_64, all with the same stack
  (excerpts: Detent/.scratch/detent/groups-upload-loop-stall/ci-probe-excerpts.txt):
  1. Run 44613773204, job 135882671999 (main 5935efa): gap 1,040 ms.
  2. Run 44535229542, job 135614852865 (e62cc77): gap 1,740 ms.
  3. Run 44472936768, job 135387007770 (d7091f6): gap 1,575 ms.
  4. Run 44467383652, job 135565218285 (f427934): gap 4,731 ms, nothing uploaded.
  5. Run 44465252451, job 135361420028 (main 84608cc): gap 3,530 ms.

  From 1 (sockets elided):
  ```
  upload:   554 ms  6.0 MiB 3 beats; links 1/1 sent 14 dropped 0 reads 9; [49622>49621 unread 43 sndq 0 sndwnd 1028864 ...] [49621>49622 unread 0 sndq 0 ...] {1.0 age 0 cpu 91 R} {1.1 age 308 cpu 0 U pc write+0xa < bwrite_conv+0x1c < bio_write_intern+0xa5 < BIO_write+0x17 < tls_retry_write_records+0x8e < ssl3_write_bytes+0x4a2 < SSL_write+0x19 < kronuz::consensus::transport::Connection::flush(...)+0x197} {2.0 age 0 cpu 0 R} {2.1 age 0 cpu 0 R}
  upload:   982 ms  6.0 MiB 3 beats; ... {1.1 age 731 cpu 0 U pc write+0xa < ... < Connection::flush(...)+0x197} ...
  upload:  1327 ms 10.0 MiB 18 beats; links 1/1 sent 32 dropped 37 reads 13; ... {1.1 age 0 cpu 12 R} ...
  ```
  From 4: host 1's connection loop sat in the same write(2) from about 260 ms to the end of the
  4 s run, with both sockets empty (unread 0, sndq 0) and sndwnd 853 KB.

  Answer: a handler that did not return, not a thread the runner did not run. Host 1's
  connection loop (1.1) is inside write(2), called by SSL_write in Connection::flush, in run
  state U (TH_STATE_UNINTERRUPTIBLE: a kernel wait without PCATCH), with 0 ms CPU, for 0.3 to
  4.6 s, while its socket's send queue is empty and the peer's window open. A thread the
  runner did not run is R. So the write blocks in the kernel on something other than socket
  space: a non-blocking socket's write can still sleep.
- In every failure the only test beside upload (ctest --parallel 2) is
  consensus_transport_connection, 9 to 14 s into its run, and upload starts as groups_reader
  ends. The earlier probe (probe/groups-upload-stall, upload beside a looping connection_test)
  failed 1 of 360; CI's macos-x86_64 failed 5 of about 15 runs on 2026-11-19.
- XNU (apple-oss-distributions/xnu main, read in .scratch/.../xnu/): x86_64 uses the mcache mbuf
  allocator (kern.ipc.mb_uses_mcache 1 here). sosend's cluster allocations are TRYHARD, but
  slab_alloc first calls freelist_populate(M_WAIT), which grows the pool through m_clalloc (a
  kernel memory allocation that can wait for pages), and every other allocator and freer sleeps
  uninterruptibly ("m_clalloc", PZERO-1) while one does; mbuf_sleep also sleeps PZERO-1. sblock
  and sbwait sleep with PCATCH (state W), so neither is this U.

- The sampler stalls with the writer. Run 4 printed samples at 258 and 517 ms and none after
  (16 expected); run 5 at 505 then 3,220 ms; run 2 at 647 then 1,352 ms; the pre-probe
  failure 44443844218 at 555 then 1,164 ms. The sampler's sockets() takes each socket's lock
  (getsockname, FIONREAD, SO_NWRITE, TCP_CONNECTION_INFO), so the writer most likely blocks
  past sosend's unlocked allocation, inside the protocol output with host 1's socket locked,
  on something global (an interface, filter or policy lock: pf, NECP, the loopback path).
  This is the hypothesis the spindump tests.
- Local reproduction, this host (x86_64 macOS 15.8.1, 32 GB, nmbclusters 262144): CI's order
  (ctest -j2 of connection, groups_credit, groups_reader, groups_upload) 12 times: 0 failures,
  no stuck sample (.scratch/.../repro1.txt). The kernel probe's line, here:
  `{kern wait +0 drain +0 16kcl 4422/6247 fail +0; vm free 2034 MiB wanted 0 pageins +236 swapins +0 decomp +218}`.

- Probe run 44634341014 (4 shards x 10 whole suites, ctest --parallel 2, macos-15-intel:
  15 GB, nmbclusters 65536, mb_uses_mcache 1, sudo works): 0 of 40 suites failed, no stuck
  sample, so spindump never ran and there is no artifact. CI 44634220600 on consensus:7fcd21f
  (the kernel counters) passed on every platform, macos-x86_64 included. No other probed
  failure since 2026-11-19 21:15Z. The kernel wait inside write(2) is not named yet.

- Probe 2 (44640924436, 4 x 10 suites at bf56a07): 0 of 40 failed. 80 probed suites, 0 stalls.
- Capture probe (44641415023, cba94dc): the held run aborted (rc 134: no TLS fixture, the
  probe's bug); 0 of 20 suites failed. One suite's upload had a loop stuck 250 ms without
  failing, and spindump ran under sudo (artifact spindump-2/spindump-1.txt, macOS 15.7.9
  24G830, Macmini6,2, 4 CPUs, 14 GB): user frames are named, kernel frames are "??? (kernel +
  N) [address]", and the loop that tripped it was a busy one (99 of 130 samples in the
  reader's sleep_for), not a kernel wait. So 7f84a09 triggers only on a U-state stuck loop and
  names kernel frames from `nm -n /System/Library/Kernels/kernel` (23,429 symbols here,
  sosend, msleep, lck_mtx_lock among them).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] Notes, A1: five probed CI failures (run ids there), all host 1's connection loop in write(2) under SSL_write in Connection::flush, state U, 0 CPU, sockets empty: a handler that did not return (a kernel wait inside write), not an unscheduled thread |
| A2 | pending |
| A3 | pending |

## Result

<pending>

## Rulings I made

- The note the lead asked for lives in docs/mechanisms/host-transport.md and a header comment in
  ci.yml (outside the quest's file set: the lead named both).

- A1's "runner or product" from the probe: U with 0 CPU inside write(2) is a call that did not
  return; which kernel resource it waits on decides whether the fix is in the product.

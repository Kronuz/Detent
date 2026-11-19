# groups-upload-3s-class: journal

State: waiting on jobs 261118-205311-962da3 261118-205312-f556f8 261118-205321-1d7dff 261118-205323-25f32e, CI 44461388834 on consensus:84608cc
Quest: .campaign/detent/quests/groups-upload-3s-class.md (the lead's; never edit it)

## Doing

Probe at consensus:84608cc (branch pushed; CI running). Jobs: suite, upload x20 with the probe, the two mutants.

## Next

1. Read the jobs and CI 44461388834 on 84608cc; fill the Evidence rows; mark ready to land the probe (lead's ruling, msg below).
2. Merge main once groups-upload-stall lands (this branch already holds its c782db1, fast-forwarded).

## Blocked on

Nothing of mine. For landing: the file set names test/groups_test.cc; the file is
test/transport/groups_test.cc, and docs/mechanisms/host-transport.md comes in with
groups-upload-stall's commits (gone from the diff once it lands and main is merged).
`campaign status` flags both; the lead's to correct.

## Messages

- Lead's ruling on the bound: option 1. Done: no retry in `timed` (it would hide a handler
  that never returns); the probe lands once its jobs and CI pass; A2 stays open in Result. A
  blocked handler in a sampled failure is a product defect: the lead grants the transport
  files then.

## Notes

- A1, run 44443844218 (macos-x86_64, job 135291207518, 02:25Z, consensus:c782db1). Log and
  excerpt in Detent/.scratch/detent/groups-upload-3s-class/. Sockets: 49626>49625 is host 1's
  (sender), 49625>49626 host 2's (receiver).
  ```
  upload:   297 ms 4.0 MiB  2 beats; sent 14 dropped 0 reads 8; [49626>49625 unread 0 sndq 0 sndwnd 766080 ... rexmit 0] [49625>49626 unread 114842 ...]
  upload:   555 ms 6.0 MiB  3 beats; sent 14 dropped 0 reads 9; [49626>49625 unread 43 sndq 0 sndwnd 1045248 ... rexmit 0] [49625>49626 unread 0 sndq 0 rcvwnd 1045248 ...]
  upload:  1164 ms 6.0 MiB  3 beats; sent 14 dropped 0 reads 9; [49626>49625 unread 43 sndq 0 sndwnd 1045248 ... rexmit 0] [49625>49626 unread 0 sndq 0 rcvwnd 1045248 ...]
  upload:  1427 ms 6.0 MiB  3 beats; sent 14 dropped 0 reads 9; [49626>49625 unread 69 sndq 0 sndwnd 1045248 ... rexmit 0] [49625>49626 unread 0 ...]
  upload:  1697 ms 6.0 MiB  3 beats; sent 15 dropped 51 reads 10; [49626>49625 unread 0 sndq 425516 sndwnd 9472 ... rto 230 rexmit 1] [49625>49626 unread 1476540 rcvwnd 9398 ...]
  upload:  1951 ms 10.0 MiB 23 beats; sent 40 dropped 61 reads 13; ...
  ```
  Where the 1,525 ms went:
  1. About 1.3 s: host 1's connection loop did not turn, from at most 297 ms to between 1,427
     and 1,697 ms. Its frames sent stood at 14, and its drops at 0 while the test posted 4
     chunks every 100 ms (enqueue drops a group's chunk past 8 MiB queued, so a turning loop
     shows drops within 400 ms). The 43 bytes of credit host 2 sent stayed unread in host 1's
     socket for 870 ms, and host 2's keepalive (26 bytes, at about 1,427 ms) joined them.
     Host 1 sent no keepalive of its own though one was due by about 1,300 ms (host 2's reads
     stayed at 9). So the loop missed three independent wakes: posts, readability and its
     own timer; it was not waiting in its reactor.
  2. Not the control lane, a window or loss: through the stall both sockets were empty
     (sndq 0, unread 0 on host 2), both windows open (about 1 MB), rexmit 0, so nothing was
     queued anywhere for a control frame to wait behind.
  3. About 250 ms after: the loop's backlog ran at once (dropped 0 to 51, 1,427-1,697 ms) and
     put one 2 MiB data frame onto the then-full path (host 2 unread 1.48 MB, rcvwnd 9 KB);
     the 20 backlogged controls went next and arrived by 1,951 ms (beats 3 to 23). That part
     is the designed cost: control first, behind at most one data frame. The rexmit 1 and
     rto 230 from 1,697 ms on are the full path's zero-window probing, not loss.
- Reproduction: a mutant that sleeps host 1's connection loop 1.3 s at 300 ms
  (consensus:b959fbf, on no branch) gives the same samples locally: unread 43 then 69 on
  host 1's socket, sent and dropped flat, then dropped 0 to 52 and beats 4 to 23 in one
  sample; gap 1,409 ms at a 22.5 MiB/s reader (bound 1,000). Output in
  Detent/.scratch/detent/groups-upload-3s-class/blocked-local.txt. Busy-loop mutant: consensus:d5cc9d7.
- Runner or product: the samples cannot tell a thread the runner did not run from a handler
  that did not return. Against a product cause: every call on the loop's path is
  non-blocking (sockets and OpenSSL non-blocking, timers on the wheel, DNS on threads), the
  transport takes no lock, and no handler does 1.3 s of CPU. The process was not stopped
  (worst host stall 78 ms on the 10 ms sleeper). The probe-3 failure (44441165298, a loaded
  runner) came 15 minutes earlier, at 02:10Z, on another runner.
- The probe (consensus:84608cc, test only): each 250 ms sample posts a stamp to every loop of
  both hosts and prints `{host.loop age A cpu C S}`: how long the last stamp has waited, the
  loop thread's CPU ms since the previous sample, its run state (macOS thread_info: R runnable,
  W waiting, U, S), and, once a stamp waits 250 ms, its pc and frame-pointer return addresses
  (thread suspended for the read, named with dladdr after it resumes). Linux prints CPU only.
  Locally at the blocked mutant: `{1.1 age 1035 cpu 0 W pc __semwait_signal+0xa <
  std::this_thread::sleep_for... < asio::detail::executor_op<...> < scheduler::do_run_one ...}`.
- What the probe tells, on the next sampled failure:
  1. W, no CPU, stack in a call (kevent included): blocked; the stack names it (product if
     kevent with work pending: a lost wake).
  2. R with CPU and a pc that moves between samples: a long handler; the stack names it.
  3. R, a pc that stays put across samples: the runner did not run the thread (a vCPU the
     hypervisor took, or starvation). Then the bound that holds is the gap less the loop's
     stall: here 1,525 - about 1,300 = about 225 ms, under max(1,000, 1.6 x 644).

## Evidence

| Criterion | Evidence |
|---|---|
| A1 | [manual] run 44443844218 (macos-x86_64, c782db1) named in Notes: host 1's connection loop did not turn for about 1.3 s with both sockets empty and windows open, then the designed one-frame wait (about 250 ms); not the control lane, a window or loss. Reproduced by the blocked-loop mutant (b959fbf, local) |
| A2 | pending: probe at 84608cc; fails-without blocked-loop 261118-205321-1d7dff, busy-loop 261118-205323-25f32e; the runner-or-product answer waits for the next sampled CI failure (Blocked on) |
| A3 | pending: suite 261118-205311-962da3, upload x20 with the probe 261118-205312-f556f8, CI on 84608cc |

## Result

consensus:84608cc (test only, test/transport/groups_test.cc): `upload`'s samples carry, per loop
of both hosts, the stamp age, the thread's CPU ms, its run state and, once stuck 250 ms, its
stack (Notes, "The probe"). A1: the 1,525 ms gap of 44443844218 is about 1.3 s of host 1's
connection loop not turning, with both sockets empty and windows open, then the designed
one-frame wait (about 250 ms). The bound is unchanged.

Open: A2 waits for a sampled loop-stall failure, which the probe attributes (Notes, "What the
probe tells"): a blocked or long handler is a product defect (transport files granted then); a
runnable thread with a frozen pc is the runner.

## Rulings I made

- The file set's test/groups_test.cc is test/transport/groups_test.cc (no other exists).
- Branch fast-forwarded to groups-upload-stall's c782db1 (unlanded): the probe sits in the same
  sampler and summary its quest adds, so merging main after it lands is trivial.
- A probe rather than a bound change: the samples locate the stall but cannot attribute it; the
  lead ruled option 1 (no retry in `timed`).

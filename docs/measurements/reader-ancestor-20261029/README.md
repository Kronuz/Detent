# Inline ancestor-owner experiment

**Date:** 2026-10-29. **Verdict:** NO-GO; prototype discarded. Main and the resource candidate retain their original directory traversal.

One counterbalanced screen completes 80 fresh-process reads: 20 unchanged keys, private/canonical resources crossed with current/inline ancestor owners, matching per-arm warmups and rotated order. Values and all 44 explicitly traced directory open/stat/ACL/readlink operations match in every sample. Descriptor cleanup is checked separately; the trace does not record the existing file owner's destructor closes. All arms use the same malloc-event hook and trace machinery, so this is a prototype screen rather than uninstrumented acceptance.

| Resource arm | Paired median wall change | Paired median caller CPU change | Wall wins | Allocation change |
| --- | --- | --- | --- | --- |
| Private | +10.139 µs | +11.953 µs | 7/20 | -12 in every sample |
| Canonical | -7.013 µs | -6.928 µs | 11/20 | -12 in every sample |

Reduced allocation counts do not establish an end-to-end improvement. The private arm worsens and the required benefit in both arms is absent. No acceptance batch or resource adoption follows; no unchanged retry is authorized by this result.

The native ancestry test passes at the 256/257-component boundary, with inode identity, close-on-exec and descriptor conservation checks. Owner storage is 2,048 bytes plus eight bytes of depth, replacing a 24-byte vector handle and its heap owners. Clang's static stack-usage records show the helper frame increasing from 5,000 to 7,032 bytes in the private arm; the caller frame remains 3,112 bytes. Shared-arm stack records are preserved too. This does not establish the cause of the observed timing changes.

Prototype source is preserved at Detent:07f9a1a, with documentation links corrected at Detent:4df06dc. Hosted run 40856062518 supplies platform and privileged checks; its final result is recorded separately. A local privileged-path skip does not close that coverage. The full proposed header and depth test are retained alongside raw results, copied helper sources, executable/source hashes and compiler stack records.

The builder records the original scratch-layout prototype path. Reproduction requires that path to contain the immutable Detent:07f9a1a proposal, plus the matching baseline/resource builds and unchanged prepared generation. The live prototype worktree is restored after this experiment; rerunning its builder against a restored header would not reproduce these records.

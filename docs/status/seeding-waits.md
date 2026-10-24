# Seeding waits

Status: Evidence complete; no implementation proposed.

Question: Why does the 100K mixed seed take 104.338 s?

Finding: Serialized file and directory sync completion occupies most measured seed time. Saved 100K telemetry records 71.84 s through its first post-seed health snapshot. A first fresh 10K run records 5.52 s through its last in-seed snapshot and 5.92 s through its first post-seed snapshot; the follow-up records 5.96 s through its last in-seed snapshot. This is submission-to-reaping occupancy, not syscall time.

Verification: Both 10K current-source runs applied 80 transactions and verified the full 10,000-record state. The follow-up seed wall time was 10.363 s; storage-driver time summed 7.254 s by the last health snapshot. Probe processes and host holds exited.

Next: Instrument the actual fallback sync syscalls and the client-to-server request stages in a 10K diagnostic run if exact attribution is needed. Do not change durability or library defaults from this evidence alone.

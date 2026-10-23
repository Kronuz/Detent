# Seeding waits

Status: Evidence complete; no implementation proposed.

Question: Why does the 100K mixed seed take 104.338 s?

Finding: Serialized file and directory sync completion occupies most measured seed time. Saved 100K telemetry records 71.84 s through its first post-seed health snapshot; the fresh 10K run records at least 5.92 s during its 10.084 s seed.

Verification: 10K current-source run applied 80 transactions and verified the full 10,000-record state. Probe process and host hold exited.

Next: Instrument the remaining client-to-server stages in a 10K run if the residual needs attribution. Do not change durability or library defaults from this evidence alone.

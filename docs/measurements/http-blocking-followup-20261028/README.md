# Blocking HTTP cost follow-up

These complete exclusive screens keep HTTP adoption blocked. Both use the corrected state-based gzip fixture, identical dependency closures, three alternating-order pairs, 500 requests per 100-knob process and 20 per 100K-knob process. The reservation and batching changes are local library prototypes over http:c64fb43, not final public-pin qualification. Exact patches and binary hashes are in the metadata.

| 100K coding | Reservation before / after caller CPU per request | Reservation plus batching before / after |
| --- | --- | --- |
| Identity | 5.533 / 8.095 ms | 4.763 / 5.393 ms |
| Gzip | 12.209 / 12.534 ms | 11.132 / 10.726 ms |
| Zstd | 8.175 / 5.005 ms | 6.674 / 4.651 ms |

Each entry is the median of three process-batch caller-CPU averages. Compare before and after within each screen; different baseline medians prevent treating the difference between screens as an isolated causal effect. Small-response caller CPU remains about 8–9 microseconds higher with the batching candidate; the 100K identity difference is +13.2%. Neither cost gate closes.

The 100K fixture is 11,018,028 decoded bytes, with response wire sizes of 11,018,146 bytes (identity), 571,605 bytes (gzip) and 333,395 bytes (zstd). Names and definitions are generated and repetitive; these compression ratios are not a real-corpus or fleet claim. Every response is compared with the generated payload, and paired wire and payload identities must match.

The native full HTTP suite passes 14/14 for reservation and 14/14 for reservation plus batching. The composed consumer at http:c64fb43 passes six hosted jobs in run 40675608079. The later library commits http:cd3b2b6 and http:bd3e8e1 preserve these two changes separately; their hosted qualification remains pending.

The common driver and build script follow the [initial screen](../http-blocking-screen-20261028/README.md). `paired_reserve.py` and `paired_batch.py` retain separate outputs and failure captures. Run compilation through `quiet light` and measurements through `quiet exclusive`. Timing includes connection construction, teardown and body comparison; process CPU includes the fixture server, and peak RSS includes setup. These plain blocking results do not qualify TLS, streaming delivery, tail latency or client-only RSS.

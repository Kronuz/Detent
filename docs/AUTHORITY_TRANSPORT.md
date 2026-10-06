# Authority transport implementation

**Status:** Bounded codecs and owned TLS primitives implemented. Production peer listeners, publisher HTTPS and multi-voter service configuration remain pending under the [validated service blueprint](ASYNC_AUTHORITY_ARCHITECTURE.md).

The transport code lives in `src/detent_authority/transport/`. It depends on the strict consensus types and OpenSSL 3.0 or later, without modifying the legacy cluster protocol or existing durable encodings. The optional authority build links these primitives through `detent_authority_transport`; the current daemon still uses its existing single-voter HTTP path.

## Admission and encoding

`DTHL0001` is a fixed 120-byte peer hello carrying the node ID, control/bulk lane, cluster/group/restore epoch, fresh 128-bit connection identity and 256-bit semantic fingerprint. Certificate admission establishes voter identity independently of the hello. The fingerprint includes the canonical application limits, consensus limits and sorted fixed voter membership; it excludes the local voter ID. A different quorum or application profile fails admission before replication payloads are accepted.

`DTC1` uses explicit stable control tags and little-endian integers. Booleans, entry kinds, snapshot correlation, counts and trailing bytes are checked. Entry counts are bounded before vector allocation; both per-command and aggregate payload limits apply. Raft's `rpc_bytes` limits payload rather than framing, so the checked maximum Append wire size is `68 + 32 * rpc_entries + rpc_bytes`, before the outer four-byte length prefix. A full legal 2 MiB payload round-trips.

Peer TLS requires chain verification and a SHA-256 leaf-certificate pin bound to one configured voter. Configuration checks reject duplicate pins and a local certificate that differs from the local voter pin. Publisher clients verify the chain and requested DNS name or IP address; publisher servers retain the separate application credential check. TLS versions below 1.2, early data and renegotiation are disabled. Certificate-list admission is capped at 64 KiB.

## Owned operations

The session belongs to one authority executor. It retains its TLS context, allows one original read and one original write, and rejects application operations before authenticated admission. Handshake and hello exchange share one absolute deadline. Idle bulk lanes can remain open; after the first frame byte, one absolute header/body deadline prevents trickled input from extending the wait. Whole writes have a finite deadline.

`Frame` acquires shared byte credit before allocating its buffer and retains it through original write completion. Cancellation races reap the losing original operation before releasing that buffer. Socket error shutdown closes the transport; it does not start TLS shutdown concurrently with application IO.

`SnapshotWrite` copies SnapshotChannel's borrowed frame into owned write storage. Closing, fencing or timing out the channel cannot free those bytes during a suspended TLS write. Successful completion consumes channel output only when the trusted session, token and remaining length still match. An obsolete completion releases its own resources without consuming a replacement connection's frame. A socket write never synthesizes an Installed acknowledgment.

The [retained admission API](../../Kronuz/cluster/consensus/README.md) supplies the additive Worker API needed to hold one decoded control event through Busy/Pressure without copying its payload on each retry. It is committed in cluster `10de37c`; the existing API and storage/wire formats remain compatible.

## Qualification and remaining work

The transport tests use real TLS records over both OpenSSL BIO pairs and asynchronous duplex pipes. They cover mutual authentication, unknown CAs, missing client certificates, unknown and duplicate voter pins, incompatible profiles, hostname verification, oversized/zero/partial frames, exhausted byte admission, stalled writes and shutdown with original reads/writes pending. Explicit coroutine cancellation preserves an operation-aborted result rather than misreporting a deadline. Pipe tests qualify the asynchronous stream contract and cancellation mechanism, not a TCP deployment. Snapshot ownership tests retire the borrowed allocation while its owned copy remains charged and reject stale session/token/offset completion.

The four transport suites and certificate fixture pass natively (3.70 seconds) and under ASan/UBSan (4.03 seconds) with OpenSSL 4.0.3. Directly compiled authentication and asynchronous pipe tests also pass with OpenSSL 3.6.5. The full application build succeeds; a broader selection passes 23 of 24 suites, with the TCP diff-cache test unable to connect to its local server. Existing authority state, both Node/cluster scheduling profiles, service and process-recovery tests pass. These results do not replace the complete-component acceptance gates.

The real TCP test remains registered as `test_authority_transport_tls`. After restoring session permissions, it passes over real loopback TCP (2.76 seconds including certificate setup). Authenticated three-process failure/scale tests remain unqualified until service integration is complete. The earlier permission-limited run could not bind TCP and could not connect to the diff-cache server; those failures do not qualify network behavior.

Remaining service work includes bounded pre-handshake connection admission, per-voter control/bulk queues, reconnect generations and deterministic dial ownership, Worker/SnapshotChannel dispatch, a stream-generic HTTPS path with owned plaintext read-ahead, multi-voter configuration and completion barriers across shutdown. Incremental consensus CPU work and other-platform native qualification remain separate open gates. Review the complete service after these gates pass; this module is not Stage 4 completion.

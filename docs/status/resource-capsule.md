# Capsule and canonical resource qualification

**State:** Composed source, unbuilt and unqualified. Production adoption remains blocked.

The candidate combines the complete capsule implementation and stash safety correction from `Detent:b02f7dc` with the previously reviewed EX-1 resource integration from `Detent:52246e2`. The capsule-aware `DiskLedger::paged_peak` method remains identical to the private-resource candidate. No new generic resource mechanism is added.

1. Allocators stays at `allocators:b7daa52`, completion at `completion:21809c7`, journal at `journal:dbf753f`; consensus and unrelated pins remain fixed, including the corrected stash and LRU revisions.
2. Full native product tests, publisher/verifier exclusion, rollback lifecycle and clean public-pin hosted qualification remain required.
3. Uninstrumented resident-data first reads must pass 200/200 below 1 ms. The passing twenty-key prototype screen is not acceptance.
4. CL-2 combined directory/file ownership remains independent and unactivated. The current publisher and verifier use the same legacy file-lock protocol.
5. Legacy remains the writer default; the capsule default decision follows complete qualification and owner approval.

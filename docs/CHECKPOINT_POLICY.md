# Authority checkpoint scheduling

Node initiates checkpoints when committed entries or retained payload bytes reach half their configured limit. A due condition now remains latched until actual published base advancement. Capture requires an empty application slot, matching State and Worker applied boundaries, and an applied boundary beyond the current base. An unavailable replacement reservation leaves the condition due and retries after 100 logical milliseconds.

The scheduler tries the acknowledged root immediately after Worker advances Applied, before Node takes the next delivered range. This preserves a capture opportunity under sustained application backlog. Existing Worker machinery owns preparation, original IO, publication and reclamation; there is no separate Service maintenance loop. Healthy local preparation remains valid across role changes.

The deterministic regression seeds 32 committed entries and delivers them in four-entry ranges without advancing election time. Linking the prior Node (`6d667cf`) into the same fixture fails its early-capture assertion. The updated policy captures before the backlog finishes and publishes a new base under both synchronous and owned-completion storage. An election-enabled fixture did not establish attribution because persistence itself created a capture gap.

Final policy qualification passes six native macOS checks in 58.11 seconds, six ASan/UBSan checks in 67.52 seconds and six Linux checks in 51.07 seconds. The associated three-process HTTPS fixture keeps a follower offline while the other voters compact beyond its last commit, then requires nonempty snapshot catch-up, continued writes, a strong read and an exact receipt after another leader replacement. Those transport fixtures and health fields belong to the ongoing readiness integration; these results do not mark that component complete.

Threshold headroom cannot guarantee completion under unlimited offered load or indefinitely slow storage. This policy does not add periodic captures of an unchanged base. Resource pressure waits or rejects admission; uncertain storage failures retain existing fencing behavior.

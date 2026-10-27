# Bounded lookup diagnostic

These are the scripts and observer for the [40-process lookup record](../resource-lookup-diagnostic-20261027.json). Copy the directory to `.scratch/resource-adoption-20261027/lookup-diagnostic/` in the recorded qualification workspace. `build.py` copies and instruments only `lazy_page_access.cc`, substitutes its object in a copied archive and links the common caller diagnostic. The existing baseline and reader builds, corpus and key manifest are prerequisites. Original archives are unchanged.

Run `quiet check`, then `build.py` through `quiet light` and `run.py` through `quiet exclusive`. The fixed screen covers the first 20 keys in the recorded manifest with alternating before/after order and a separate matching warmup process for every measurement. Source-factory and Client construction remain required. Lookup substep observers use caller CPU and process-wide fault counters, add overhead and do not satisfy the acceptance timing gate.

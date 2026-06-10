# Manifest

## Marketing sources used

Primary publication cues:

- `marketing/04_SOCIAL_MEDIA.md`
  - public repositories:
    - `hesia-pqc-bench`
    - `optee-ta-skeleton`
    - `pqc-migration-guide`
  - explicit rule: do not publish the HESIA core code
- `marketing/06_CONTENT_MARKETING.md`
  - outline for the PQC migration guide and the benchmark positioning
- `marketing/05_CAMPAGNES_PUBLICITAIRES.md`
  - public benchmark material referenced as outbound collateral

## Internal repo material reviewed before curation

- `drone_transition_source/optee_ta_skeleton/`
- `docs/HESIA_TA_OPTEE_REFERENCE.md`
- `README_02_FONCTIONNEMENT.md`

## Included here

- a generic benchmark repository scaffold for PQC measurements on embedded targets
- a generic MIT-licensed OP-TEE TA skeleton repository
- a CC-BY-SA migration guide repository with checklists and reusable snippets

## Explicitly excluded

- `drone_source/`
- `server_source/`
- any HESIA product logic, secure-channel implementation, or runtime policies
- production UUIDs, anchors, sealed blobs, signatures, allowlists, and operator workflows
- customer, security, or deployment secrets

## Reasoning

The marketing plan calls for open repositories that create trust and technical credibility without exposing the proprietary core product. This staging area follows that rule strictly.


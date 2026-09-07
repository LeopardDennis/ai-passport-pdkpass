<p align="right">
  <a href="CHANGELOG.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Changelog

## Unreleased

- Fixed standings scheduling at race end + 30 minutes and bounded retries after
  failed synchronization; retained confirmed schedules after partial downloads.
- Prioritized current-weekend results, isolated historical retry deadlines,
  migrated result cache v2 to v3 without discarding podiums, and serialized
  season/results transactions to prevent stale writes during season changes.
- Streamed season JSON one object at a time, grew bounded small-response buffers
  on demand, and shared HTTP rate-limit backoff across the two data services.
- Made setup passwords random, rejected overlapping provisioning submissions,
  committed only the tested credentials, and kept a clock synchronized during
  this boot usable across Wi-Fi reconnects; added an NTP error state.
- Reused visible calendar/standings rows, sampled battery data outside LVGL,
  dispatched buttons through a queue, stopped idle timer polling after screen-off,
  and made bounded USB screenshot capture an opt-in debug feature.

- Reworked the bilingual GitHub project READMEs around the PDKPASS experience,
  added production-UI simulator captures for three contrasting rounds, and
  surfaced the controls, app-free setup, simulator, build path, documentation,
  project status, and disclaimer from the repository landing page.
- Made each race-detail session row state-aware: upcoming sessions retain their
  Beijing start time, while completed, pending, cancelled, and absent sessions
  show `RESULT`, `PENDING`, `CANCELLED`, or `NO SESSION` respectively.
- Changed the results podium cards from fixed gold, white, and orange to each
  classified driver's current team colour, with automatic text contrast.
- Replaced the Mac simulator's fixed sample podium with real per-round,
  per-session OpenF1 historical results, asynchronous loading states, and a
  persistent local cache.
- Made the calendar background and status accent follow the currently selected
  circuit theme while browsing rounds with Up and Down.
- Added a native macOS PDKPASS simulator that runs the production LVGL UI and
  navigation with keyboard controls, network-state previews, and PNG capture.
- Auto-fit home-screen country names from their measured pixel-font width so
  long names retain side margins instead of being clipped on the narrow screen.
- Added 23 distinct circuit-based background themes shared by the home and
  race-detail screens, while retaining the original blue within the season.
- Replaced ambiguous `UP/DN` navigation hints with complete `UP/DOWN` labels.
- Rebuilt every PDKPASS screen around the approved pixel race-pass artwork,
  including paper-ticket shadows, UNSCII type, race-themed gradients, per-race
  accent panels, a circuit-outline detail card, and overflow-safe compact rows.
- Replaced the shared approximate circuit drawing with distinct offline,
  low-resolution outlines for all 23 rounds of the 2026 season, selected by
  circuit name so calendar round changes do not mislabel known tracks.
- Replaced the hardware demo menu with the offline-first PDKPASS application:
  next-race dashboard, current-season calendar, full driver standings, race
  details, race-specific colours, battery status, and idle backlight control.
- Added a hardware-independent PDKPASS navigation state machine and host tests,
  plus fork-owned bilingual product READMEs and an official-data snapshot note.
- Added the read-only `FAP_SCREENSHOT_V1` serial capture service required for
  verified AI Passport community release covers.
- Added app-free browser provisioning, persisted Wi-Fi after connection
  verification, six-hour network time synchronization, offline time fallback,
  Beijing-time automatic race selection, and a season-complete home state.
- Added delayed, unauthenticated OpenF1 historical-result sync for FP1, FP2,
  FP3, sprint qualifying, sprint, qualifying, and race podiums, with low-rate
  retry, offline NVS caching, and a per-round results screen.
- Added year-aware OpenF1 season synchronization for up to 24 Grands Prix and
  24 drivers, Beijing-midnight and post-race checks, guarded new-year adoption,
  offline season persistence, and a compact year-isolated podium cache.
- Made mini-program BLE install compatibility a template-level invariant: fixed
  protected `cardid`/Recovery partitions, retained the five-second UP-key
  Recovery boot hook, and added CI validation for merged-image structure,
  partition MD5/ranges, the 3 MB app limit, and protected payload exclusion.
- Documented a release-title convention for multi-app releases: name tags as `v<version>-<app-name>` (e.g. `v0.1.0-voice-keychain`) so the release title carries the version and the app, and confirm the title after the release is published so a release list is scannable by app.
- Added a post-release follow-up workflow: an `issue-suggestions` skill for filing user feedback as issues against the upstream project, an `experience-pr` skill for submitting reusable development experience as a documentation PR, a `docs/experiences/` directory for per-entry experience files, and supporting `project-completion`, `file-issues`, and experience-index documents.
- Simplified the tracked repository root: moved GitHub-recognized community documents into `.github/`, moved the changelog into `docs/`, updated every reference, and added a root-document allowlist to repository checks.
- Repository-wide language policy: every maintained Markdown default `.md` file is English, Simplified Chinese uses a paired `.zh_CN.md`, and both provide language switches. Static checks reject missing peers, missing switches, and Chinese prose in English defaults.
- Phase one of the AI development workflow: streamlined task-based context routing, unified local/CI validation, added PR checks and a template, and committed the dependency lock for reproducible builds.
- PR review fixes: pinned GitHub Actions to full commit SHAs, split build/release jobs by least privilege, disabled persisted sync checkout credentials, added Feature Request and Usage Question forms, clarified private security-report fallback, and corrected stale README, CI-trigger, and branch descriptions.
- Changed commit titles, PR titles, and PR bodies from Chinese-default to English; updated the Chinese punctuation rule so it no longer applies to PR descriptions.
- Reworked `build-firmware.yml` to pass `SDKCONFIG_DEFAULTS=sdkconfig.defaults`, enable `partitions.csv`, preserve the 8 MB image header, merge a flashable `FoloToy-AI-Passport-full.bin`, publish only that artifact, and use Actions cache v5.
- Integrated upstream PR #6 to resolve PR #4 conflicts: Wi-Fi, Bluetooth LE, radio lifecycle, and low-power demos; a 3 MB factory partition; build/menu/configuration updates; hardware-guide coverage; and bilingual capability tables.
- Defined English imperative Conventional Commit formatting for both commits and PR titles.
- Removed stale sync-workflow template comments and generalized an irrelevant Redis TTL rule to cache components.
- Added Chinese punctuation, credential safety, and recoverable file-deletion conventions.
- Expanded source-comment requirements for functions, state, ownership, concurrency, timing, registers, and magic values.
- Removed AI execution instructions from product READMEs so they remain human-facing product and repository overviews.
- Added `docs/development/agent-guide.md` as the focused AI workflow guide.
- Updated `AGENTS.md`, `docs/INDEX.md`, and the development index for the agent guide.
- Documented why the root README path is reserved for fork owners and how GitHub README precedence supports it.
- Created `main-update` from the upstream-aligned baseline and combined the repository-structure, firmware-CI, and upstream-sync work.
- Corrected the merged documentation index, workflow path, project tree, and CI references.
- Moved CI documentation from software design to `docs/development/`.
- Moved fork-only documentation assets from `assets/docs/` to `docs/assets/`.
- Moved the upstream English/Chinese project READMEs under `docs/` and renamed the documentation catalog to `docs/INDEX.md`.
- Initialized `AGENTS.md`, `CLAUDE.md`, and `CHANGELOG.md`.
- Standardized the initial project README language filenames.
- Added the `docs/`, `assets/`, and `skills/` directory structure.
- Moved the upstream hardware guide into `docs/hardware-design/`.
- Standardized subdirectory README capitalization and introduced fork conventions.
- Allowed fork-owned root README and supplemental documentation content on fork `main`.
- Added and documented the fork-only supplemental-document directory.
- Moved the build CI document to its dedicated CI branch before consolidation.
- Documented clean-`main` reasons, the direct-development exception, and Actions enablement for forks.
- Split the original agent rules into contribution, development, and fork documents with a compact root index.
- Updated software-design and project README references for the new documentation structure.
- Added the documentation catalog and task-triggered routing based on the earlier repository model.
- Added bilingual contribution, code-of-conduct, security, and support documents tailored to this ESP-IDF and fork workflow.

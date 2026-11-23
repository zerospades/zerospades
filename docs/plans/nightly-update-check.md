# Plan: nightly builds notice a newer nightly

## Goal

A build published as the `nightly` release should tell its player when a newer
nightly is out, the way release builds already announce a newer stable release.
Release builds and local dev builds keep their current behaviour.

## Where things stand

- `Sources/Gui/UpdateChecker.cpp` asks `releases/latest` once per launch, in the
  background, and compares the tag (`vX.Y.Z`) against the
  `ZEROSPADES_VERSION_*` macros. `releases/latest` never returns prereleases,
  so the nightly is invisible to it.
- A nightly is built from the same `CMakeLists.txt` version as the stable it
  follows, so to the current check a nightly looks like that stable release.
  The checker shows "Up to date (0.0.9)" and no new nightly ever shows up.
- A build does not know it is a nightly. The only trace of where it came from is
  `GIT_COMMIT_HASH` (`Sources/CMakeLists.txt`): a short hash whose length can
  vary, `"?"` without git, and always `"?"` in the Nix build, because the flake
  source has no `.git`.
- `.github/workflows/ci.yml` `publish-nightly` force-moves the lightweight tag
  `nightly` and recreates the `nightly` prerelease with
  `--target ${{ github.sha }}`. It runs on push to `main` and on
  `workflow_dispatch`, which can be started from **any** branch.
- UI consumers of the result: `UpdateNotice` (banner and "Up to date" mark),
  `UpdatePromptScreen` (startup modal, gated by `cl_zsUpdatePrompt`),
  `OfficialModsNotice` and `MainScreenMainMenu` (mods vs game version).

## Design

### 1. The build knows it is a nightly, and its exact commit

Add one CMake cache variable:

```cmake
set(ZEROSPADES_NIGHTLY_COMMIT "" CACHE STRING
    "Full commit SHA of a published nightly build; empty for any other build")
```

When it is set, configure two extra lines into `ZeroSpades.h`:

```c
#define ZEROSPADES_NIGHTLY 1
#define ZEROSPADES_NIGHTLY_COMMIT "<40-hex sha>"
```

CMake checks that the value is 40 hex characters and fails the configure step
otherwise. Use a full SHA, not `GIT_COMMIT_HASH`: it is exact, it does not need
git at build time (which Nix lacks), and it matches what the API returns.

Only CI sets the variable. Local builds, PR builds and distro packages stay
"not a nightly" and keep the stable-release check.

### 2. CI passes the commit only to builds that become the nightly

In `ci.yml`, compute the condition once at workflow level, using the same
condition as `publish-nightly`:

```yaml
env:
  NIGHTLY_COMMIT: ${{ (github.event_name == 'push' && github.ref == 'refs/heads/main' || github.event_name == 'workflow_dispatch') && github.sha || '' }}
```

- Windows x86/x64, macOS Intel/ARM: add `-D ZEROSPADES_NIGHTLY_COMMIT="$NIGHTLY_COMMIT"`
  (PowerShell: `$env:NIGHTLY_COMMIT`) to the configure step.
- Ubuntu: same, on the `cmake ..` line.
- Nix: add a `packages.nightly` output to `flake.nix` that overrides
  `cmakeFlags` with `-DZEROSPADES_NIGHTLY_COMMIT=${self.rev}` (`self.rev` is
  only defined for a clean checkout, which CI always has). The job runs
  `nix build .#nightly` when `NIGHTLY_COMMIT` is set and `nix build` otherwise.
  Also bump the stale `version = "0.0.8"` while editing the flake.

PR builds get no commit, so a binary downloaded from a PR's artifacts never
claims to be a nightly.

### 3. The check asks about the nightly release

In `UpdateChecker::Query::Fetch`, choose the endpoint by build type:

| Build   | Request                                               | "Newer" means                             |
|---------|-------------------------------------------------------|-------------------------------------------|
| release | `GET /repos/zerospades/zerospades/releases/latest`    | tag version > compiled version (unchanged) |
| nightly | `GET /repos/zerospades/zerospades/releases/tags/nightly` | see below                             |

For a nightly:

1. Read `target_commitish`. CI passes a SHA to `--target`, so this is the
   published commit. If it is not 40 hex characters (a release created on an
   existing tag names a branch instead), read the commit from
   `git/ref/tags/nightly`; if that fails too, return `Failed`.
2. If it equals `ZEROSPADES_NIGHTLY_COMMIT`, the state is `UpToDate`.
3. Otherwise call `GET /repos/zerospades/zerospades/compare/{mine}...{theirs}`
   and read `status` and `ahead_by`:
   - `ahead`: `UpdateAvailable`, and keep `ahead_by` as the number of new commits.
   - `identical` or `behind`: `UpToDate`. This can happen when a dispatch from
     an older branch replaced the nightly.
   - `diverged`: `UpdateAvailable` with no commit count. The nightly was
     rebuilt from another branch, so the player is still on a build that is no
     longer published.
   - The request fails: `Failed`, so the game stays quiet as it does for any
     other network error.
4. Use `releaseUrl = https://github.com/zerospades/zerospades/releases/tag/nightly`.
   It already passes `IsReleasePageUrl`.

This takes two unauthenticated requests per launch at most. GitHub allows 60
per hour per IP, which is plenty.

Why the release and not `git/ref/tags/nightly`: the workflow moves the tag
**before** it deletes and recreates the release. Checking the release means a
player is told only once the binaries are there to download. A request that
lands during the few seconds between the delete and the create gets a 404 and
ends as `Failed`, so nothing is shown.

A nightly build does **not** also check `releases/latest`. The nightly after a
stable tag already contains that release, and showing "0.0.9 is available" to
someone on a nightly built after 0.0.9 would be wrong.

### 4. Result and UI

Extend `UpdateChecker::Result`:

```cpp
bool nightly = false;          // this build is a nightly
std::string latestCommit;      // short (7) SHA of the published nightly
int commitsAhead = -1;         // -1 when unknown (diverged)
```

Add `static bool IsNightlyBuild()` and `static std::string CurrentNightlyCommit()`
(short form) next to `ReleaseVersion::Current()`.

Change the text shown to a nightly player:

- `UpdateNotice` banner: "A newer nightly is out ({0}, {1} new commits; you have
  {2}). Click to open the release page." When the commit count is unknown, drop
  that part.
- "Up to date" mark: "Up to date (nightly {0})".
- `UpdatePromptScreen`: "A newer ZeroSpades nightly is available!\n\nInstalled:
  {0}\nLatest: {1}\n\n…" Keep the same buttons and the same behaviour (open the
  release page and close the game).
- `OfficialModsNotice` / `MainMenu`: the "needs a newer ZeroSpades" link points
  to the nightly page for nightly builds instead of `releases/latest`. The mods
  version gate itself is unchanged.

Every new or changed string goes through `_Tr("MainScreen", …)`. Then run
`pot-update.sh`.

### 5. Do not nag on every push

`main` gets several pushes a day, so a modal on every launch would get old
fast. Add `cl_zsNightlyPromptedCommit` (archived string, default empty):

- The **modal** prompt shows only when the published nightly commit differs from
  the stored value. Store the commit once the player has answered either way.
- The **banner** always shows while a newer nightly exists.
- `cl_zsUpdatePrompt = 0` still turns the modal off entirely.

Release builds ignore the new cvar.

### 6. Logging

Mirror the existing log line, e.g.
`Update check: running nightly 1a2b3c4, latest nightly is 5d6e7f8 (12 commits ahead, update available).`

## Files touched

- `CMakeLists.txt`: the cache variable and its validation.
- `ZeroSpades.h.in`: the `#cmakedefine`-style nightly defines.
- `Sources/Gui/UpdateChecker.{h,cpp}`: channel selection, nightly fetch,
  compare call, new result fields.
- `Sources/Gui/UI/Components/UpdateNotice.cpp`: captions and the prompt-once
  cvar.
- `Sources/Gui/UI/Components/OfficialModsNotice.cpp` and
  `Sources/Gui/UI/MainScreen/MainMenu.cpp`: the nightly link and wording.
- `.github/workflows/ci.yml`: `NIGHTLY_COMMIT` and the configure flags.
- `flake.nix`: the `nightly` package output.
- Translation template, via `pot-update.sh`.

## Verification

1. Configure locally with `-D ZEROSPADES_NIGHTLY_COMMIT=<sha of an older main
   commit>` and launch with `--override-cvar s_audioDriver=null`. Expect the
   banner, the modal once, the "N commits ahead" count, and the log line.
2. Relaunch. Expect the banner but no modal (the cvar now holds the commit).
3. Configure with the current nightly's SHA. Expect "Up to date (nightly …)".
4. Configure with a malformed value. Expect the CMake configure step to fail.
5. Configure without the variable. Expect the stable behaviour, unchanged.
6. Run with no network. Expect nothing shown and a `[!] Update check:` log line.
7. CI: open a PR and confirm its artifacts are not nightly builds (check the log
   line). After the merge, download the published nightly and confirm the log
   line names its own commit as up to date.

## Open questions

- Should a nightly player also be told when a **stable** release is newer than
  their nightly's base version (for example someone stuck on a months-old
  nightly)? This plan says no: the newer nightly already contains that release.
- Should `workflow_dispatch` keep publishing the nightly from any branch? The
  `diverged` handling covers it, but limiting the publish to `main` would make
  "newer nightly" always mean "ahead on main".

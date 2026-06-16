# AGENTS.md

Guidance for AI agents and contributors working in this repository.

## Overview

`lxd-pkg-snap` is the official snap packaging repository for [LXD](https://github.com/canonical/lxd),
a system container and virtual machine manager. It builds the [LXD snap](https://snapcraft.io/lxd).
The codebase is mostly POSIX shell, with a small Go helper tool (`lxd-snapcraft`) and a C utility (`shmounts`).

## Repository structure

- `snapcraft.yaml` — main snap definition (parts, apps, hooks, layouts).
- `snapcraft/` — runtime components:
  - `commands/` — command entry points (`lxd`, `lxc`, `daemon.*`, etc.).
  - `hooks/` — snap lifecycle and plug connect/disconnect hooks.
  - `wrappers/` — wrapper scripts (gpu, kmod, sshfs, etc.).
  - `etc/` — config files (e.g. logrotate).
- `lxd-snapcraft/` — Go tool to read/update `version` and `source-commit` fields in `snapcraft.yaml` while preserving comments.
- `shmounts/` — C utility (`setup-shmounts.c`) built via `make`.
- `patches/` — patches applied to upstream dependencies (edk2, lxcfs, nvidia) during build.
- `lxd-qemu-snap/` — separate snapcraft definition for the QEMU variant snap.

## Critical conventions

### `source-commit` rule

Every `source-commit:` line in `snapcraft.yaml` has the form:

```yaml
source-commit: <sha1> # <version-tag>
```

**Always update both fields together.** When bumping a dependency, the SHA1 *and* the
version comment must change at the same time. Updating only the comment silently pins the
build to the wrong upstream revision. See [.github/copilot-instructions.md](.github/copilot-instructions.md).

### PR target branch

Pull requests target a specific channel branch (e.g. `latest-edge`, `5.21-edge`). The target
branch may be encoded in the PR title suffix, e.g. `Bump LXD (5.21-edge)`; it defaults to `latest-edge`.

## Build

Local builds require the LXD snap installed (snapcraft uses it for the build container):

```sh
snapcraft pack
```

Multi-architecture builds use Launchpad builders:

```sh
snapcraft remote-build --launchpad-accept-public-upload --build-for amd64,arm64
```

## Test and verify

These run in CI ([.github/workflows/tests.yml](.github/workflows/tests.yml)) and should pass locally:

```sh
# Unit tests for the Go helper tool
cd lxd-snapcraft && go test -v ./...

# Verify every source-commit SHA1 matches its version comment
cd lxd-snapcraft && go run . -file ../snapcraft.yaml -verify-source-commits
```

Shell script changes are validated by Differential ShellCheck in CI and must be clean.

Build the C utility with:

```sh
cd shmounts && make
```

## Coding conventions

- **Shell**: use `#!/bin/sh` with `set -eu`; keep scripts shellcheck-clean. Use a
  justified `# shellcheck disable=<code>` comment only when necessary. Prefer shell
  parameter expansion over spawning external commands.
- **Go** (`lxd-snapcraft`): standard library plus `yaml.v3` (which preserves comments);
  cover changes with tests under `lxd-snapcraft/testdata/`.
- **C** (`shmounts`): built with gcc and `-Wall`; keep it minimal and dependency-free.

## Security

Report security vulnerabilities upstream as described in [SECURITY.md](SECURITY.md); do not
file them in this packaging repository.

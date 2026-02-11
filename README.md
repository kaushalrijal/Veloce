# Veloce

Veloce is a portable terminal-based version control prototype written in C.
It supports account management, repository management, commit history, and revert workflows while persisting data locally.

## What Works

- Signup, login, and password reset with salted SHA-256 hashes.
- Repository creation and listing per user account.
- Repository initialization with either:
  - an existing tracked file, or
  - a new tracked file created in the local workspace.
- Commit creation with message and file snapshot storage.
- Commit history viewing.
- Reverting tracked file content to a previous commit (and recording that revert as a new commit).

## Cross-Platform Support

The app is implemented for:

- macOS
- Linux
- Windows

Platform-specific operations (`getch`, clear screen, sleep, directory creation) are wrapped behind portable helpers.

## Build

### Option 1: Make

```bash
make
./vcs
```

Sanitizer build:

```bash
make sanitize
./vcs
```

### Option 2: CMake

```bash
cmake -S . -B build
cmake --build build
./build/vcs
```

On Windows:

```powershell
.\build\Debug\vcs.exe
```

## Storage Layout

All runtime data is stored under `.veloce/` in the project root by default:

- `.veloce/users.db`
- `.veloce/repos.db`
- `.veloce/commits.db`
- `.veloce/snapshots/`
- `.veloce/workspace/`

You can override the storage directory by setting `VELOCE_HOME`.

## Notes

- This is a learning project and not a replacement for Git.
- Hashing is implemented with a local SHA-256 routine and salt; no plaintext credentials are stored.

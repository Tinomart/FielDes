# Known issues

What you should know before you spend time on something. A problem that is not here: **Help ▸ Report a bug** opens a new issue with the version and your system filled in.
What a tool does and does not do (its scope, its accuracy, its cost) is in the guide, next to the tool: [docs/manufacturing.md](docs/manufacturing.md), [docs/analysis.md](docs/analysis.md),
[docs/fields.md](docs/fields.md), [docs/interface.md](docs/interface.md); the README has the design decisions (*Known limitations*).

## Platforms

- **Windows** is the main platform. The setup program and the zip are not signed: Windows SmartScreen may say *unknown publisher* the first time; *More info ▸ Run anyway*
  opens it (the source is public, and the setup is built from it by the script in `scripts/`).
- **Linux** packages (a folder, a `.run` installer, a `.deb`) are built on Ubuntu 22.04 (glibc 2.35), so they open on Ubuntu 22.04, Debian 12 and everything newer. They were run under WSL2
  (Ubuntu 22.04, with WSLg), not on every distribution.
- There is **no macOS** build.

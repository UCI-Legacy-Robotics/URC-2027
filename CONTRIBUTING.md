# Contributing to URC-2027

See [docs/](docs/README.md) for setup and building.

**`main` is the tested version of the rover.** Develop, demo, and test on your branch, and merge to `main` only once your changes have been tested.

## Workflow

1. **Start from the latest `main`.**
   ```bash
   git checkout main && git pull
   ```
2. **Create a branch** named `<your-name>/<subsystem>/<short-description>`. Use your first name or first name plus last initial:
   ```bash
   git checkout -b ethans/drive/teleop-deadzone
   ```
   Subsystems match the `src/` folders (`drive`, `arm`, `cv`, `telemetry`, ...), `gui` for the base station GUI, `mavlink` for the MAVLink dialect, or `docs` for docs-only changes.
3. **Open a draft PR right away, even if nothing works yet.**
   ```bash
   git push -u origin ethans/drive/teleop-deadzone
   ```
   On GitHub, choose **Create draft pull request**.
4. **Develop and test on your branch.** It's fine for a branch to stay open for weeks while you work and test. Push regularly so your progress is visible in the draft PR.
5. **Once it's tested** (below), mark the PR **Ready for review**.
6. **Merge once approved.** The branch is deleted automatically.

Branches with no commits for 3+ weeks get checked in on by leads.

Stay up to date with `main` often, especially on long-running branches:

```bash
git fetch origin && git rebase origin/main
```

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <short description>
```

The scope is the subsystem, the same one used in your branch name. It's optional for changes that span the whole repo. Write the description in the imperative mood, in lowercase, with no trailing period.

| Type       | Use for                                          |
| ---------- | ------------------------------------------------ |
| `feat`     | a new feature or capability                      |
| `fix`      | a bug fix                                        |
| `refactor` | a code change that neither fixes nor adds        |
| `perf`     | a performance improvement                        |
| `test`     | adding or fixing tests                           |
| `docs`     | docs only                                        |
| `build`    | CMake, `package.xml`, dependencies               |
| `ci`       | CI workflows                                     |
| `chore`    | anything else (tooling, cleanup, repo config)    |

Examples:

```
feat(drive): add deadzone to joystick teleop
fix(arm): clamp joint velocity before sending to controller
docs: add base station network diagram
```

For breaking changes, such as a renamed topic or a changed message field, add `!` after the scope and explain the change in the body:

```
feat(interfaces)!: rename DriveCmd.speed to DriveCmd.linear_velocity

Nodes publishing DriveCmd must update the field name.
```

PR titles follow the same format.

## Before marking a PR ready

Run the checks for what you changed:

- Rover packages: [Rover setup → Checks](docs/setup/rover.md#checks). Make sure it builds, tests pass, and you've run it in sim or on hardware.
- GUI: [frontend checks](docs/setup/gui.md#frontend-checks) and [backend checks](docs/setup/gui.md#backend-checks).
- MAVLink dialect: regenerate on both sides, see [MAVLink dialect](docs/system/mavlink.md).

## Docs

All docs live in [`docs/`](docs/README.md) and are updated in the same PR as the change. See [Writing docs](docs/README.md#writing-docs).

Questions? Ask your subteam lead or in your draft PR.
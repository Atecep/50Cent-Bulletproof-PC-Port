# Campaign Compatibility Tracker

The community tracks campaign coverage through the **Campaign compatibility report** issue form. Use one issue per mission or checkpoint per platform and port version. If the same configuration and checkpoint already have a report, add your result to that issue.

## Baseline

| Campaign segment | Linux | Windows 11 | Evidence |
| --- | --- | --- | --- |
| Mission 0, opening gameplay | Reached | Reached | Prior development validation. Full mission completion is unconfirmed. |
| Mission 0, completion and transition | Untested | Untested | Record the last checkpoint and the next mission if reached. |
| Later missions and campaign ending | Untested | Untested | Add a row for each mission as it is reported. Use the name or number shown in-game. |

## Reporting a run

1. Record the port version or commit, supported disc region and revision if known, OS, CPU, GPU and graphics driver. Include relevant display or input settings.
2. Note the mission and starting checkpoint. State whether you began a new campaign or loaded a checkpoint, and record the furthest point reached.
3. Where the mission allows it, check objectives, cutscenes, audio, controls, checkpoint save and reload, and the transition into the next mission. Mark each unchecked item as **Not tested**.
4. If something breaks, include the objective or checkpoint, reproduction steps and a short redacted excerpt from `userdata/logs/`. Keep spoilers out of issue titles. Share results without uploading game files or saves.

## Status definitions

| Status | Meaning |
| --- | --- |
| Untested | No recorded run at this scope. |
| Reached | The segment loaded and gameplay was observed. |
| Completed | The mission objectives were completed and the transition was observed. Record checkpoint and feature checks separately. |
| Completed with issues | The mission was completed, with a reproducible issue recorded. |
| Blocked | A reproducible issue prevents progress. Record the last reachable checkpoint. |

When updating the tracker, link to the issue and record the tested build, disc revision and platform. Preserve separate results when a driver or platform changes the outcome. A successful report applies to the tested configuration and segment.

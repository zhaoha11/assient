# Context Handoff

This folder is the persistent handoff entry point for ECloudAssistant and ENET.

## Read first

1. Read `CONTEXT.md` for the canonical terms used by the signaling design.
2. Read `WORKLOG.md` for the implemented state, verification evidence, current
   limitations, and the next safe task.
3. Inspect the referenced source files before changing behavior.

## Update rule

After every code implementation or bug fix, update `WORKLOG.md` in the same
local Git commit. Add the change goal, files, protocol/data-flow effect, build
or runtime verification, rollback point, and unresolved follow-up work.

`CONTEXT.md` is a glossary only. Do not put implementation details in it.

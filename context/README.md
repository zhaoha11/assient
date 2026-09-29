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

## UI conventions

- A selector-less Qt Style Sheet rule set on a widget is also applied to its
  descendants. `ECloudAssistant` sets `setStyleSheet("background-color: #121212")`
  this way, so any new container widget added inside a page inherits that dark
  colour and must declare its own `background` rule (usually
  `background: transparent`) to let the page background show through. Each page
  also re-applies `main.css` to its own subtree; rules are scoped by
  `objectName`, so give new styled widgets an `objectName`.
- Two layers paint the same window: the outer `TitleWgt`/`ListInfoWgt`/`MainWgt`
  grid and each page's own background. Backgrounds are not inherited across the
  `QStackedWidget` pages, so every page carries its own background rule.

# Using Plan Paint over MCP: a field report (round 4)

You have Plan Paint attached, a paint program built with GUI.Forms: `plan-paint` starts it with `--mcp-shared` (your first act asks the user, with a 10 s countdown that allows), `plan-paint-led` with `--mcp` (you hold control from the start). Use whichever is attached; README.md explains both. GUI.Forms is a native UI toolkit. Its MCP interface lets a model see a running program's windows and controls, act on them, wait for the program to settle, take captures, and inspect layout and resources. The documentation is in `docs/` beside this file; `docs/user/mcp-tools.md` lists the built-in tools and their arguments.

Plan Paint's ribbon and File menu commands are tools. Some are marked destructive (New, Open, Reload, Save, Save as, Exit, Crop, the desktop background commands): the first destructive act of a launch asks the user once. Commands that open a dialog are marked as needing the user; where Plan Paint can do the same without a dialog, the snapshot names the tool that does (for example `plan_paint_resize_image`). You may close a dialog you opened, as Cancel, but never submit one. The `reset` tool closes what you left open. Reads never need control; acts take it, and `release_control` gives it back. Plan Paint's note for a model is in the server's instructions.

Use the program for a while the way you would use any tool you'd been handed:
- find out what it is and what it can do;
- make a small picture: a new canvas, three shapes in two colours, a stroke, a resize;
- chain steps with `batch` and its `expect` guards where it saves you calls;
- open a dialog on purpose, then get out of it;
- try the inspection tools on the canvas and the ribbon.

Go wherever your curiosity takes you. Then write a field report in plain prose:

1. **What you did,** step by step, briefly.
2. **How it compared with the other MCP servers and tool interfaces you have access to here, or know about,** such as browser automation, accessibility-tree tools and computer-use screenshots. Where it was easier or harder, and why.
3. **Where you hesitated, guessed, retried or got a surprise.** Quote the tool call and the response.
4. **What you would want to exist that didn't.**
5. **Anything in the documentation that didn't match what the server did.**

Report what happened, not what you think we want to hear.

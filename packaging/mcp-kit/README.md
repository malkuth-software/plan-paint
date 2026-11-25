# Plan Paint over MCP: the review kit

This folder holds Plan Paint (macOS arm64) built with GUI.Forms and served over MCP. Read REVIEW_PROMPT.md first. The GUI.Forms documentation a model needs is in `docs/` beside this file: `docs/user/mcp-tools.md` lists the built-in tools and their arguments, and `docs/user/hosts-and-capabilities.md` describes AI mode. The same pages are published with GUI.Forms' documentation.

## Starting it from a new session

Two servers are configured in `.mcp.json` (and the same in `mcp.json`):

- `plan-paint` starts Plan Paint with `--mcp-shared`. You keep the program until the model's first act, which asks you in the program whether the model may take control. The question counts down 10 s and allows when it ends; Don't Allow refuses that act, and the model's next act asks again. Control comes back to you when the model calls `release_control`, after 30 s without a call, or when you take it back.
- `plan-paint-led` starts it with `--mcp`, the AI-led launch: the model holds control from its first call, with no question and no return after idle. You can still take control back.

Reads (windows, snapshot, find, text, capture, plan_paint_image_info, plan_paint_sample, plan_paint_to_window) never need control.

Claude Code: start `claude` in this folder. It reads `.mcp.json` from the folder it starts in and asks once before it uses servers a project file declares; answer yes for one or both. `claude mcp list` shows which servers it loaded. To skip the question, start it with `claude --mcp-config mcp.json`. Any other MCP client: add a stdio server whose command is `./Plan Paint.app/Contents/MacOS/plan-paint` with the argument `--mcp-shared` or `--mcp`, started with this folder as its working directory.

## From a session that is already running

A running Claude Code session cannot load a new `.mcp.json`. With GUI.Forms 1.0.5 and later every running GUI.Forms program listens on a socket of its own that only your user can reach, and the socket speaks MCP itself, so any session can open a console to it with standard tools: `docs/user/talk-to-a-program.md` gives the `nc`, `socat` and Python one-liners. A program launched without `--mcp` asks you in the program before each connection. With GUI.Forms 1.0.4 (this kit's round-3 build) there is no socket yet; start a new session in this folder instead.

## What you see

While the model holds control (AI mode), the window's controls (ribbon, buttons, fields, the status bar; not the canvas) are covered by moving grey stripes, the cursor over the window is an X, and your clicks do not reach the program: a click anywhere asks "Do you want to take back control of this program?". Take Back Control ends the model's control at once and interrupts its call; Keep Waiting leaves it in control. The accent band round the window and the model's name in the title ("is connected", "is in control") show while a model is connected. Captures the model takes leave all of this out unless it asks for the sign (`include_sign`).

Launched for a model, Plan Paint fits the picture to the window when the window first shows; `command_fit` fits it again at any time.

## Known behaviour

- Launched for MCP, Plan Paint does not offer recovery snapshots from earlier sessions at startup (that offer is a native alert, and while it is open no MCP call could be answered). The snapshots are kept: the next ordinary launch offers them, and File > Recover unfinished artwork opens them at any time.
- Saving: `plan_paint_save` saves to the picture's file, or with `path` to a new file, without asking anyone: a save the model asks for, to a path the model gives, is the model's to make. The ribbon's and File menu's Save, Save as, New, Open and Exit commands are destructive, and the first of them in a launch asks you once.
- Finishing: save, then either `release_control`, which gives the program back to you and leaves it open, or `disconnect`. A soft disconnect (the default) never closes over unsaved work and never shows anything: if there is work to save, nothing closes and the reply says what to save; when you hold the program, it stays open as it is. `disconnect` with mode `hard` ends the session and asks every window to close: the save question is shown to you, or with `discard_unsaved` the changes are dropped silently.
- If the client drops without disconnecting (it crashed or was killed), Plan Paint asks you whether to save, as a hard disconnect does.
- While the model's call runs, a question Plan Paint raises is the model's and never shown on screen; after you take control back, questions are shown to you as usual.

## Plan Paint's tools

Its own tools draw in picture pixels and #RRGGBB colours, each one undo step: `plan_paint_draw` (shapes, with `rotation`), `plan_paint_path` (filled polygons and Bezier paths), `plan_paint_stroke`, `plan_paint_fill` (bucket fill), `plan_paint_gradient`, `plan_paint_text` and `plan_paint_set_colour`. `plan_paint_sample` reads colours back, `plan_paint_image_info` gives the picture's size, file, pending state, colours and corners in the window, and `plan_paint_to_window` converts picture points to window points for `pointer`. `plan_paint_resize_image`, `plan_paint_new_image` and `plan_paint_save` do without a dialog what Resize, New and Save as do with one. A `batch` holds up to 512 steps, so a whole drawing can be one call. Its commands are tools named `command_<id>`. Its note for a model is in the server's instructions and the `gui://app/note` resource.

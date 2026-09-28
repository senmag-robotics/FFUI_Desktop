# FFUI Tutorial — Design Spec

Status: **draft for review, round 2** — all of Gareth's answers folded in below; Stage 5b
rewritten to match the now-decoupled dictation/AI-assistant setup (see "Dictation vs. AI
assistant setup" section). Not yet implemented.

## Global rules

- **Voice**: all tutorial narration uses `pAssistantVoice` (the AI assistant's voice), but the
  **FFUI narrator's** speed setting — not the assistant's own speed setting — so it's
  distinguishable by voice alone from ordinary FFUI prompts, while still respecting the one
  speed control the user already knows about for narration pacing.
- **Auto-start**: the very first time the program is ever launched, before anything else happens,
  play the auto-start announcement (see Stage 0) and enter the tutorial automatically.
- **Manual launch**: a new "FFUI Tutorial" tile in the FFUI Settings room re-enters the tutorial
  from the beginning at any time.
- **Two distinct skip mechanisms — do not conflate these:**
  1. **Abandon the whole tutorial**: double-press *any* button. **Only offered at the very start**
     — announced once, in the auto-start announcement (Stage 0), and active only from that point
     until Stage 0 hands off to Stage 1. Confirmed by Gareth: *"double tap to skip the tutorial
     should only be an active option at the very beginning - after that, it should no longer
     apply."* From Stage 1 onward, a double-press is just an ordinary double-press of whatever
     button was pressed (no special tutorial meaning) — the only way to leave the tutorial early
     from there on is finishing it or closing the program.
  2. **Skip just the current stage**: press *select* during a stage's opening "press select now
     to skip" window **only** — not at any other time during the stage. Confirmed by Gareth:
     *"pressing select to skip a stage should also only be active for a few seconds after the
     prompt - not all the time."* Advances to the next stage; does not exit the tutorial. Once
     the window closes, a select-press goes back to meaning whatever it normally means in context
     (e.g. confirming a menu item, dropping a dragged program).
- **Per-stage skip window**: `#define TUTORIAL_STAGE_SKIP_WINDOW_SECONDS 5` — each stage opens
  with its intro narration, then this window during which a select-press skips *that stage* only.
  Gareth: *"lets keep this as a #define so we can adjust? - maybe start with 5 seconds?"* (revised
  from the original draft's 2–3s placeholder to match).
- **Repeat-until-done checkpoints**: several stages wait on a physical action (crossing a wall,
  pressing a specific button, dropping a program) and periodically repeat a reminder until it
  happens. `#define TUTORIAL_REPEAT_REMINDER_SECONDS 5` — same starting value as the skip window,
  per Gareth's answer above (both are separate `#define`s, so each can be tuned independently once
  tried on hardware; they just happen to share a starting value for now).

## Stage numbering

Split from the original draft's "Stage 3" (which bundled room layout, quickslot cycling, the
Program Tray, drag-and-drop, and FFUI Settings into one stage) into separate numbered stages,
so a future addition slots in without renumbering everything around it:

| # | Stage |
|---|---|
| 0 | Intro |
| 1 | The Stylus |
| 2 | Haptics vocabulary |
| 3 | The Program Slot (main room) |
| 4 | The Program Tray |
| 5 | FFUI Settings, dictation, and the AI assistant |
| 6 | Tutorial end |

---

## Stage 0 — Intro

**Entry**: automatic on first-ever launch, or first line spoken on manual re-launch from the FFUI
Settings tile.

**Narration** (auto-start only — manual launch skips straight to "Welcome to FFUI..."):
> "This is your first time launching FFUI. I am auto-starting the tutorial mode — double-press
> any button to skip."

- This is the only point in the whole tutorial where a double-press means "abandon the tutorial"
  — see Global Rules above.

**Narration** (both entry paths):
> "Welcome to FFUI — this project explores the Windows operating system as a physical 3D space
> you can explore with force feedback.
>
> You can turn off the stylus at any time by pushing it all the way back onto the desk."

**Exit**: proceeds immediately to Stage 1. No interactive checkpoint here. From this point on, the
double-press-to-abandon gesture is no longer active for the rest of the tutorial.

---

## Stage 1 — The stylus

**Entry**: automatically, right after Stage 0's welcome line.

**Narration** (opening script — subject to the normal skip window, per Global Rules):
> "Let's go over the stylus's buttons."
>
> "The stylus has a number of buttons. On top, there are two, one in front of the other — these
> are the select button, at the front, and a right-click button, at the rear. On the side, there's
> an AUX button, and a scroll wheel."

**Revised (round 10)**: Narrate moved off the rear button onto a standalone AUX tap — "tap to
narrate, hold to dictate, tap+hold for the AI assistant... keep the back button to right click".
The rear button is now purely an OS right-click (already implemented before this round; it used to
fire alongside Narrate on every press, which this remap also cleans up — see NARRATE_SPEC.md).
Wording above updated accordingly.

The second line was added after later hardware-test feedback ("can we add an overview of buttons
and locations at the beginning") — purely locational (no function is explained here, only where
each button sits), so it doesn't reintroduce the deadspace/no-checkpoint problem the rewrite below
already fixed; it isn't gated on a press of its own.

**Revised after hardware testing**: originally this stage named every button in one long,
uninterrupted block with no checkpoint (see the opening script above, which is all that's left of
that original block). Feedback from the first two rounds of testing changed this to a
press-to-continue tour instead — each button gets its own instruction, then waits for a press of
that specific button before moving to the next:

> "The front button, under your fingertip, is Select — press it to interact with whatever you're
> touching: choosing a menu item, or grabbing and dropping a program. Go ahead and press it now."
- *(waits for a Select press)*

> "The side button is Aux. Tap it once, on its own, to hear what the stylus is currently on —
> that's called Narrate. Hold it to dictate speech into a text field. Tap it once, then hold, to
> talk to the built-in Claude AI assistant instead. Go ahead and press it now."
- *(waits for an Aux press)* — updated (round 10) to name the new standalone-tap Narrate gesture
  alongside the two AUX gestures already taught here.

> "The scroll wheel under your thumb also works as a middle button. Go ahead and press it now."
- *(waits for a middle-button/scroll-click press)* — deliberately does NOT mention quickslot
  cycling here ("we didn't introduce the concept of slots yet"); quickslots get their own proper
  introduction in Stage 3 instead.

> "The rear button sends a right-click, the same as right-clicking with a mouse, once you're
> browsing a program. It won't do anything right now, but go ahead and press it to finish this
> tour."
- *(waits for a rear-button press)* — updated (round 10): the rear button's real function is now
  an OS right-click (already implemented; only unwired here because `mouseSuspended` covers this
  whole stage — see "Also revised after hardware testing" below), not the placeholder Narrate
  wording this line used before Narrate moved to AUX. Scroll-forward/backward remain unwired, per
  "Future functionality, deferred" below.

**Exit**: proceeds to Stage 2 once the rear button is pressed.

**Also revised after hardware testing**: normal FFUI haptics (walls, tile detents, everything) and
mouse control are both suppressed for the whole of Stage 0/Stage 1 — "there should be no haptic
effects until they are introduced later" and "suspend mouse control during the first stages" — see
Implementation notes (v1) below. The FFUI narrator (as opposed to the tutorial's own narrator) is
additionally muted outright for the whole of Stage 1 AND Stage 2, so nothing it might otherwise say
(e.g. if the user happens to wander into the active room mid-tour) can surface before it's relevant.

---

## Stage 2 — Haptics vocabulary

**Entry condition**: from Stage 1.

**Skip**: "Press select now to skip" + `TUTORIAL_STAGE_SKIP_WINDOW_SECONDS` window, as above.

**Precondition**: pause all other haptic rendering for the duration of this stage — the four demo
vibrations are triggered directly by the tutorial script, not by the user finding a real instance
of each on screen. Confirmed by Gareth as the right approach for now: *"demo vibrations are
triggered directly by the script - I think this is the right approach for now."*

**Narration + demo sequence**:
> "The stylus uses force feedback to present UI elements as solid surfaces, and to create
> vibration effects. Different vibrations tell you what you're hovering over."

- "This means you're at a hard boundary, such as the screen edge." → play the edge/boundary
  vibration (`SolidPlane`'s existing buzz).
- "This means you're hovering over a text field." → play the text-field vibration
  (`CursorStateHaptics::TextField`).
- "This means you're hovering over clickable text — for example, a hyperlink." → play the
  clickable/hand vibration (`CursorStateHaptics::Clickable`).
- "This means the program is busy or unresponsive." → play the unresponsive vibration
  (`CursorStateHaptics::Unresponsive`).

> "Some surfaces are solid; others will pop through if you push."

**Exit**: proceeds to Stage 3 once narration completes — narration-only for the solid-vs-pop-
through distinction, no added interactive checkpoint. Confirmed by Gareth: *"Solid vs. pop-through
wall - I think it is ok just to make sure they know this distinction exists - they will experience
it themselves in due course."*

---

## Stage 3 — The Program Slot (main room)

**Entry condition**: from Stage 2.

**Skip**: "Press select now to skip" + window.

**3a. Guidance into the room**

> "I am guiding you towards the main room, called the Program Slot."

- Apply a guidance force toward the center of the current program slot.
- Repeat the narration every `TUTORIAL_REPEAT_REMINDER_SECONDS` until the user arrives.
- On arrival: suspend the guidance force, resume full FFUI interface rendering.

> "This is where you can work with your active program. While in here, you are controlling the
> mouse cursor on the active program, and getting haptic feedback from it."

**3b. Quickslot cycling**

> "You have a number of quick slots in which you can store different programs. To swap between
> them, use the scroll click — try it now."

- Repeat every `TUTORIAL_REPEAT_REMINDER_SECONDS` until the middle button (scroll click) is
  pressed.
- On press: play the normal "Slot N, {program name}" narration (existing `narrateSlotSwitch`
  behavior) — no separate tutorial-specific confirmation needed.

**3c. Crossing into the front zone**

> "If you push forward, you'll feel a wall. Behind it are the Program Tray, on the right, and
> FFUI Settings, on the left. To reach them, gently push through this wall — you'll feel a pop."

- Repeat every `TUTORIAL_REPEAT_REMINDER_SECONDS` until the user crosses into the front zone
  (either side).

**3d. Routing to the Program Tray**

- If the user lands in FFUI Settings (left) instead of the Program Tray (right):
  > "Move to the Program Tray, on the right-hand side — you'll feel another pop-through wall
  > in the middle of the two rooms."
  - Repeat every `TUTORIAL_REPEAT_REMINDER_SECONDS` until they cross to the right side.
- If the user lands directly in the Program Tray (right): proceed straight to Stage 4, no
  extra prompt needed.

---

## Stage 4 — The Program Tray

**Entry condition**: from Stage 3d, once the user is in the Program Tray.

**4a. Orientation**

> "This is the Program Tray. Here, you can assign your open programs to quickslots to use in
> the main Program Slot room. Every program running on your machine is represented as a tile.
> When you move to a new tile, FFUI reads out the program name."

- Wait for any FFUI narration in progress to finish before continuing.

**4b. Populating an empty tray**

A genuinely fresh install may have nothing else open to drag — Gareth confirmed this is "a true
challenge" and asked: *"could we fill it with 3 'dummy' programs (just during the tutorial) if
none are open?"* Resolution: yes — this is now the spec, not an open question.

- On entering Stage 4, check the real Program Tray. If it is empty, spawn 3 placeholder tiles
  that exist **only for the duration of the tutorial** (removed the moment the tutorial ends or is
  abandoned, and never written to the real window-tray state) — e.g. "Tutorial Program 1/2/3",
  purely as drag-and-drop targets with no real window behind them. If the tray already has at
  least one real program, use those instead and skip the placeholders entirely.
- Implementation note (for build time, not a design question): the cleanest place for this is
  probably a tutorial-only flag on `WindowWallObject`/`ObjectsFactory` that synthesizes up to 3
  fake tiles when the tutorial is active and the real tray is empty, rather than teaching
  `WindowManager` a generic notion of a "fake window" — keeps the fakery contained to tutorial
  code and away from the real window-tracking path.

**4c. Assigning a program**

- Wait for the user to select a new tile (i.e., FFUI narrates a program name) and for that
  narration to finish.
> "To assign a program to a quickslot, hover over its tile, hold the select button, and pull it
> forward. You'll feel it pop between the quickslots. Let go of the button to drop it in one."
- Repeat every `TUTORIAL_REPEAT_REMINDER_SECONDS` until a program (real or placeholder) is dropped
  into a slot.

**4d. Confirmation**

- Wait for any FFUI narration in progress to finish.
> "After assigning a program, FFUI automatically loads that quickslot in the main room."

**Exit → Stage 5**: routing narration depends on which room the user is currently in:
- If in the Program Slot (main room): *"Push forward and to the left to find the FFUI Settings
  room."* — repeat every `TUTORIAL_REPEAT_REMINDER_SECONDS`; if they instead cross into the
  Program Tray, adjust the prompt to route from there instead.
- If in the Program Tray: *"Push left to find the FFUI Settings room."* — repeat every
  `TUTORIAL_REPEAT_REMINDER_SECONDS`; if they instead cross into the main room, adjust the prompt
  to route from there instead.

---

## Stage 5 — FFUI Settings, dictation, and the AI assistant

**Entry condition**: user has crossed into the FFUI Settings room.

**5a. Orientation**

> "This is the FFUI Settings room. Moving between tiles just reads out their name — nothing
> happens until you press select, and every menu you open has a Back option to bring you right
> back here."
>
> "There are tiles here for adjusting your quickslots, the FFUI narrator's voice, the AI
> assistant's voice, launching programs by voice, and replaying this tutorial — feel free to
> explore them later. For now, you don't need to touch any of them: dictation and the AI
> assistant are both turned on using the AUX button instead, which I'll walk you through next."

Rewritten after later hardware-test feedback ("some of the language is a bit confusing and
vulnerable to extra confusion in case the user starts clicking on menu items"). The original
single line had gone stale — it called the first tile "the Start Menu" when the real tile is
labelled "Start programs", and never mentioned the room had grown to 5 tiles total (quickslots,
FFUI narrator, AI narrator, Start programs, FFUI Tutorial), several of which didn't exist yet when
that line was first written. It also had a structural gap: this stage's own checkpoints (5b/5d
below) never ask the user to touch a tile at all — they ask for an AUX-button hold instead — so a
room that visually reads as "settings to click" invited exactly the kind of exploratory clicking
that could leave someone stuck in an unfamiliar submenu mid-checkpoint. The rewrite explains
hover-vs-select up front, names the Back-item safety net, and explicitly says none of the tiles
are needed for what comes next.

### Dictation vs. AI assistant setup — corrected from round 1

Round 1 of this spec described dictation and the AI assistant as sharing **one** combined setup
sequence, based on how the code worked at the time. Gareth challenged that: *"I had thought the
speech-text engine and the claude function were technically separate (although the claude plugin
is dependant on the speech-text)? - it should be possible to have the speech-text for Dictate
without having the claude AI plugin installed / enabled."*

That's correct, and it's now fixed at the code level (see `AssistantSetup`'s new `speechState` /
`runSpeechOnly()`, alongside the existing `state` / `run()`): dictation and the AI assistant are
two **independent** opt-ins.
- Enabling **dictation alone** now only downloads the local whisper.cpp speech engine — no Claude
  CLI install, no browser sign-in.
- Enabling the **AI assistant** still requires the full sequence (Claude CLI install + a one-time
  browser sign-in), and provisions speech as part of that if it isn't already done — so someone
  who enables the AI assistant first gets dictation "for free" too, but not vice versa.

Stage 5 below reflects this as two separate prompts.

**5b. Enabling dictation**

- If dictation isn't already set up:
  > "You can dictate text into any text field. This needs a one-time setup, which just downloads
  > a small speech-recognition file — no account needed. Hold the AUX button now, and I'll bring
  > up the choice — you can go ahead with it, or decide not to, right there."
  - Reuse the existing two-detent confirm/cancel prompt (`SetupConfirmationPrompt`), which already
    defaults to Cancel. Reworded from an earlier "...and I'll walk you through it" after
    hardware-test feedback ("make it clear that the user will have the choice to set up dictation
    / AI assist or not after the hold aux — right now it could feel like it's pulling you into
    setting it up without an exit path") — the exit path already existed (Cancel is the prompt's
    safe default), this just says so up front instead of reading like a commitment.
- If already set up: skip straight to 5c.

**5c. Using dictation** (only narrated if dictation setup is complete)

> "You can now dictate into any text field. Hold the AUX button — you'll hear two beeps, meaning
> the microphone is listening. Release AUX when you're done; you'll hear two more beeps
> confirming it's finished listening."

**5d. Enabling the AI assistant**

- If the AI assistant isn't already set up:
  > "There's also a Claude AI assistant built in, which can answer questions for you. This needs
  > a one-time setup too. Tap the AUX button once, then hold it, and I'll bring up the choice —
  > you can go ahead with it, or decide not to, right there."
  - Reuse the existing two-detent confirm/cancel prompt - same wording fix as 5b above, same reason.
- If already set up: skip straight to 5e.

**5e. Using the AI assistant** (only narrated if AI assistant setup is complete)

> "Tap the AUX button once, then hold it — you'll hear a different beep, indicating Claude is
> listening. Release AUX when you're done; you'll hear another beep confirming Claude has stopped
> listening."

---

## Stage 6 — Tutorial end

> "Thanks for completing the FFUI tutorial! You can re-launch it at any time by selecting the FFUI
> Tutorial tile in the FFUI Settings room."

---

## Persistence

Gareth: *"lets make sure all the persistent settings are written into one ffui-config file on
disk somewhere? (appdata or similar?)"* — this is broader than just the tutorial's own "have I
shown this before" flag: `VoiceSettingsManager` and `QuickSlotsSettingsManager` already persist
their own settings somewhere today (not yet confirmed exactly where/how — needs a quick read of
both at implementation time), and the plan is to consolidate all of it, going forward, into one
file rather than adding yet another separate storage mechanism just for the tutorial.

Proposed shape (to confirm once `VoiceSettingsManager`/`QuickSlotsSettingsManager`'s current
storage is actually read):
- One file, e.g. `%LOCALAPPDATA%\FFUIDesktop\ffui-config.json`, alongside the existing
  `%LOCALAPPDATA%\FFUIDesktop\whisper\` folder `AssistantSetup` already uses.
- Sections for whatever `VoiceSettingsManager`/`QuickSlotsSettingsManager` currently track,
  migrated in from their existing storage the first time this ships (so nobody's existing settings
  reset), plus a new `tutorial` section holding at least `hasCompletedTutorialOnce: bool`.
- A single small settings-file class (new, e.g. `AppConfig`) that both existing managers and the
  new tutorial state read/write through, rather than each owning its own file/registry key as they
  may do today.

This consolidation is a larger, standalone piece of work in its own right (touches two existing
managers, not just new tutorial code) — flagged here so it's not forgotten, but scoped separately
from the tutorial's own state machine at implementation time.

## Mid-tutorial interruption

Gareth: *"I think it's ok to just restart from 0."* If the program closes or crashes partway
through the tutorial, the next launch (whether auto-triggered because `hasCompletedTutorialOnce`
is still false, or manually re-launched from the Settings tile) always starts again at Stage 0 —
no resume-from-last-stage logic needed for v1.

## Future functionality, deferred

Gareth: *"yes we'll implement these functionalities in FFUI in future, and implement the tutorial
for them in future too."* Applies to:
- The Narrate (rear) button's actual function (re-narrating current focus).
- Scroll-forward/backward.

Both are named in Stage 1 today purely as "here are the stylus's buttons," with no functional
explanation and no tutorial coverage beyond naming them — intentional for v1. No action needed
until the underlying FFUI functionality itself exists.

## Implementation notes (v1)

The tutorial has now been built (`TutorialFlow.h`/`.cpp`, plus small hooks in `FFUIDesktop.cpp`
and the new `AppConfig`). A few points where the shipped v1 differs from the draft above, or adds
something beyond it:

- **Stage 4's "3 dummy programs" (4b) is deferred.** Confirmed by reading `ObjectsFactory.cpp`
  that there's no dedicated tray-tile class to hook into — the whole Program Tray is a
  `WindowWallObject` list that gets *rebuilt from scratch* every scanner cycle (roughly 30 times a
  second), not a stable list you can append placeholders to. Synthesizing fake tiles safely would
  mean changing that per-scan-cycle window-synthesis path, which felt too risky to do blind,
  without a compiler, in this first pass. v1's actual behavior: if the tray is empty on entering
  Stage 4, it narrates *"Your program tray is currently empty, so there's nothing to try this step
  with right now. Once you have another program open, you can practice this from the FFUI Tutorial
  tile in FFUI Settings. For now, let's move on"* and skips straight to Stage 4's exit routing,
  rather than waiting on a drop that could never happen. If you'd like the dummy-tile version
  properly built, it's a good candidate for a focused follow-up once `ObjectsFactory.cpp`'s
  synthesis path has been read through in more depth.
- **Stage 4's exit routing (the "push toward FFUI Settings" prompt) is one generic instruction**
  regardless of which of the two front-zone rooms you're currently standing in, rather than the
  two room-specific phrasings sketched in the draft above — a deliberate simplification to reduce
  risk in this first pass, since branching the wording by origin room added complexity for a
  fairly minor narration difference.
- **A new safety cap, not in the original spec**: `TUTORIAL_MAX_REMINDERS_BEFORE_AUTO_ADVANCE` (6).
  Every repeat-until-done checkpoint (Stages 3/4/5) now gives up and moves on after 6 reminders
  even if its condition was never met — added because, without it, declining a Stage 5 setup
  prompt outright (rather than just not doing it yet) would otherwise leave that checkpoint
  waiting forever with nothing left to satisfy it.
- **Stage 1's script**, left as a placeholder gap in the previous draft, was written directly into
  code and is now also captured above under "Stage 1 — The stylus."
- **Persistence** landed as `%LOCALAPPDATA%\FFUIDesktop\ffui-config.ini` (a flat `key=value` text
  file, matching the format `VoiceSettingsManager`/`QuickSlotsSettingsManager` already used
  privately) rather than the `.json` sketched above — simpler to parse/write by hand without
  pulling in a JSON library. Keys are namespaced (`voice.*`, `quickslots.*`, `tutorial.*`) inside
  the one file. Both existing managers migrate their old private `.ini` file into this one
  automatically, once, the first time they run after this update, so nobody's existing settings
  are lost or reset.
- The tutorial is **additive, not exclusive** — it observes button presses and room position
  passed in from `FFUIDesktop::updateFrame()` and only ever adds a small guidance force (Stage 3a)
  or demo vibration (Stage 2) on top of everything working completely normally. It doesn't take
  over button handling the way the Start Menu or a settings menu does, so ordinary drag-and-drop,
  quickslot cycling, and menus all still work exactly as before, tutorial running or not.
- **How to test**: it auto-starts on the very next fresh launch (nothing in `ffui-config.ini` yet
  marks it complete). After that, it's re-launchable any time from the new "FFUI Tutorial" tile in
  FFUI Settings, without needing to wipe `ffui-config.ini` or reset anything else.

## Hardware-test fixes (rounds 2–3)

None of this could be verified without a compiler, so several things only surfaced once Gareth
actually tested on real hardware. Fixed across two follow-up rounds:

- **Double-press-to-abandon window** widened from 500ms to 1500ms — too tight for two deliberate
  presses.
- **Haptics and mouse control suppressed during Stage 0/Stage 1** — the aggregate force is zeroed
  and mouse forwarding disabled for the whole of the intro/stylus-tour stages, since force feedback
  isn't introduced until Stage 2.
- **The two narrators weren't actually speaking concurrently** despite being separate `ISpVoice`
  objects — SAPI's default priority queues speech sharing the same output device. Fixed by setting
  `SPVPRI_OVER` on both voices in `initDesktop()`.
- **The repeat-reminder timer restarted from when a reminder STARTED speaking**, not when it
  finished, so a long reminder line could trigger the next one almost immediately. Fixed to restart
  the countdown from the moment narration actually stops. Interval also lowered from 5s to 3s.
- **The Stage 3a guidance force** was gated behind both the stage's opening line finishing and the
  full 5-second skip window elapsing, and was tuned too weak (0.0005/0.003) relative to this
  codebase's own real force range. Loosened to start the moment the stage begins and raised to
  0.0015/0.006.
- **General checkpoint deadspace**: most checkpoints' own instructions were only ever spoken via
  the reminder timer, sometimes seconds late; several transitions had no announcement at all until
  a reminder eventually fired. Every checkpoint now announces its instruction immediately (guarded
  against colliding with still-playing narration) rather than waiting on the reminder cadence.
- **Skip-window vs. checkpoint-watching were needlessly sequential**: a stage's own
  checkpoint-watching used to wait for the full skip window to elapse before it could even start,
  even though the skip window's only real job is keeping a select-press-to-skip option live for a
  few seconds. Checkpoint-watching (and the guidance force it can drive) now starts the instant a
  stage's opening line finishes; only Stage 0 and Stage 6 (pure timed narration, no real checkpoint)
  still wait out the full window.
- **Stage 4's "tray empty" check false-triggered** — `ArchivedWindows` is deliberately cleared
  elsewhere in the app to build drag placeholders during any window grab, and only repopulates on
  the next scan cycle after release, so a single frame's reading isn't trustworthy. Now requires a
  full second of continuous emptiness before committing to the empty-tray narration.
- **Stage 1 rebuilt as a press-to-continue tour** (see Stage 1's own section above) rather than one
  uninterrupted narration block — each button is introduced, then waited on, before moving to the
  next. Its scroll-wheel/middle-button line was also reworded to drop the quickslot-cycling mention
  ("we didn't introduce the concept of slots yet") — that's Stage 3's job.
- **Tutorial narration was starting before the FFUI narrator's own triggered narration** (e.g. the
  same button press that satisfies a checkpoint also fires a real room-focus/slot-cycle
  announcement) — `waitForFfuiNarratorIdle()`'s poll could catch pSapiVoice in the brief gap before
  a just-issued `Speak()` call actually registers as speaking. A fixed 200ms delay before the poll
  starts mediates this.
- **The FFUI narrator (pSapiVoice) is now fully muted, not just ducked, for the whole of Stage 1
  and Stage 2** — rather than gating every individual narration call site across the codebase,
  its volume is set to 0 for those two stages (reusing the existing volume-ducking mechanism in
  `initiatePeriodicScanner()`) and restored automatically once Stage 2 ends.

## Hardware-test fixes (round 4)

- **The Stage 1/2 mute from the previous round didn't actually mute anything on hardware**
  ("the muting hasn't worked - i'm still hearing FFUI narrator prompts in this first stage").
  Root cause: the mute decision runs on FFUIDesktop's separate scanner thread, and reads
  `TutorialFlow::currentStage()` to make it — but `TutorialFlow` was documented and built as
  entirely haptic-thread-owned, so that one read was the sole place a second thread reached into
  its state, unsynchronized. A plain (non-atomic) enum read on one thread has no guarantee of
  ever observing a write made on another, so the scanner thread could keep computing the mute
  decision from a stale stage value indefinitely - explaining why the mute never visibly took
  effect. Fixed by making `TutorialFlow::stage` `std::atomic`, matching how every other
  piece of state this codebase already shares across threads is handled (VoiceAssistant's
  `captureRequested`/`exchangeInProgress`, this same class's own `narrationInProgress`/
  `activeDemoVibration`). Every existing read/write site kept compiling unchanged, since
  `std::atomic<Stage>` still implicitly converts to/from `Stage`.

## Hardware-test fixes (round 5)

- **The Stage 1/2 mute still didn't work even after round 4's atomic fix** ("the FFUI narrator
  is still audible during the initial tutorial stages... I assume it probably gets unmuted every
  time the FFUI triggers a narrator prompt?"). The atomic fix closed a real race, but the actual
  mechanism - ducking `pSapiVoice`'s volume to 0 from the scanner thread - was never reliable
  enough on its own to guarantee a muted utterance stays silent relative to exactly when SAPI
  applies a `SetVolume()` call versus a `Speak()` call already in flight. Rather than keep
  chasing that timing, muting now also happens at the source: every `pSapiVoice->Speak()` call
  site across `WindowWallObject.cpp`/`MenuSystem.cpp`/`StartMenuFlow.cpp` (14 in total) now goes
  through a new `SpeakFfuiNarration()` wrapper (`FFUIDesktop.h`), which checks the new
  `TutorialFlow::shouldMuteFfuiNarrator()` and simply never hands a muted utterance to SAPI at
  all - there's no SAPI-internal timing left to get wrong. The scanner thread's volume-ducking
  block stays in place underneath this as a harmless second layer, and remains the sole mechanism
  for the separate, never-reported-broken assistant-floor 50% duck.
- **The dictation/AI-assistant setup prompts in Stage 5 kept repeating after the user had already
  started setup, and cut off the setup flow's own dialogue** ("the 'hold the aux button now to
  set up dictation' prompt repeated after I had already pressed the button, then overrode the
  narrator prompt from the menu... same issue with the AI assistant setup too"). Root cause: both
  the tutorial's own repeat-reminder and `AssistantSetup`'s real-time setup progress narration
  speak through the same `pAssistantVoice`, each with `SPF_PURGEBEFORESPEAK` - and the reminder's
  wait condition was the setup's own terminal `Ready`/`Failed` state, so it kept firing every
  `TUTORIAL_REPEAT_REMINDER_SECONDS` for the entire, often much longer, duration setup was
  actually in progress, purging whatever the setup flow was in the middle of saying. Fixed by
  giving `waitForCondition()` a `suppressReminder` parameter: both Stage 5 checkpoints now hold
  the reminder countdown (without treating the checkpoint as satisfied) the moment setup leaves
  `NotStarted`, so the setup flow's own narration keeps the floor uninterrupted until it reaches
  a terminal state.

## Hardware-test fixes (round 6)

- **FFUI narrator prompts were still audible in the tutorial's opening stages** even after round
  5's call-site-level gating ("no I am still getting FFUI narrator prompts during the tutorial
  first stages - these need to be completely blocked until we start introducing the middle button
  to change quickslots"). Root cause: `shouldMuteFfuiNarrator()` had only ever covered Stage 1 and
  Stage 2 - Stage 0 (Intro) was never in scope for muting at all, in any round, so anything
  narrated during the very first part of the tutorial was always going to be heard no matter how
  solid the Stage-1/2 gating became. Fixed by extending mute coverage to all of Stage 0, and, per
  the precise stop-point this feedback gave, narrowing exactly where unmuting happens inside
  Stage 1 itself: silent through subStep 0 (Select) and subStep 1 (Aux), audible again from
  subStep 2 onward - the moment `updateStage1()` actually introduces the middle button/scroll-
  wheel checkpoint. Stage 2 stays fully muted throughout, unchanged from the original "steps 1 &
  2" request. `subStep` is now `std::atomic`, same reasoning and same fix shape as `stage` in
  round 4 - `shouldMuteFfuiNarrator()` reads it now too, from whichever thread is about to speak.

## Hardware-test fixes (round 7)

- **The Stage 5 setup-reminder-repeats-over-menu-dialogue bug from round 5 was still present**
  ("need to watch for the [AUX] button hold, stop narrator repeats, and wait for the menu close
  to continue... the repeats should stop after the button is pressed, and let the menu dialogue
  take over"). Root cause: round 5's fix suppressed the reminder once `speechSetupState()`/
  `setupState()` left `NotStarted` - but holding AUX doesn't move either state away from
  `NotStarted` immediately. It first opens `SetupConfirmationPrompt`'s two-detent confirm/cancel
  prompt ("Do you want to enable...? Move up to confirm, move down to cancel, then press the
  select button"), which narrates through the same `pAssistantVoice` and keeps both setup states
  sitting at `NotStarted` for its entire duration, since nothing has actually been confirmed to
  start yet. So the reminder kept firing and purging that confirmation dialogue exactly as
  before, just during a window slightly earlier than the one round 5 addressed. Fixed by
  suppressing on `VoiceAssistant::assistantHasFloor()` instead - an existing, already-public
  signal that folds in `confirmationPromptActive` (the confirm/cancel prompt itself, start to
  close) together with `isSetupInProgress()` (the actual download/install afterward), giving one
  continuous suppressed window from the moment AUX is held through the confirmation prompt
  closing through setup finishing, with no gap for a reminder to land in.

## Hardware-test fixes (round 8)

- **Stage 4's "program tray empty" false-trigger, round 2 - "I still got a false trigger on the
  program list empty check - after I start dragging a program"**: the 1-second debounce added in
  round 2 was built for a transient scan-cycle blip, not a sustained condition - but
  `ArchivedWindows` is cleared for the entire duration of any grab (to build drag placeholders),
  and a deliberate, careful drag routinely takes well over a second. So the very act of doing what
  this checkpoint asks - grab a tray tile and drag it - was itself what triggered the "tray is
  empty, give up" fallback, just delayed by a second rather than prevented. Fixed by treating
  `WindowManager::isUserGrabbingWindow` as unambiguous proof the tray had something in it,
  skipping the empty check (and holding the debounce timer at zero) entirely while a grab is in
  progress.
- **Terminology harmonized: "Program Selector" → "Program Tray"**. FFUI's own real narration
  already calls this room "Program Tray" (see `WindowManager::narrateWindowFocus()` - "Program
  Tray, {appName}" for exactly the tiles this stage teaches), but the tutorial had independently
  named it "Program Selector" throughout - a genuine mismatch, since a user standing in the room
  the tutorial calls one thing would actually hear FFUI itself call it another. Renamed everywhere
  in narration and this spec; the internal enum names (`Stage::Stage4_ProgramSelector`,
  `Room::ProgramSelector`) were deliberately left alone, since they're internal identifiers only,
  not user-facing, and renaming them would touch many unrelated call sites for zero behavioral
  benefit.
- **Stage 1 now opens with a buttons-and-locations overview** ("can we add an overview of buttons
  and locations at the beginning") before the existing press-to-continue tour: "The stylus has a
  number of buttons. On top, there are two, one in front of the other - these are the select
  button, at the front, and the narrate button, at the rear. On the side, there's an AUX button,
  and a scroll wheel." Purely locational - no function is explained here, so it doesn't reopen the
  deadspace/no-checkpoint problem the original press-to-continue redesign fixed.
- **Stage 5's orientation line rewritten for clarity** ("some of the language is a bit confusing
  and vulnerable to extra confusion in case the user starts clicking on menu items") - see 5a's
  own note above for the full reasoning; in short, it now explains hover-vs-select, names the Back
  item as a safety net, accurately describes all 5 current tiles, and explicitly tells the user
  they don't need to touch any tile for the AUX-hold checkpoints that follow.

## Hardware-test fixes (round 9)

- **Stage 3a's guidance force eased to 0.75x** ("make the assistive drag to the program slot a
  little gentler (maybe 0.75x?)") - both `GUIDANCE_SPRING_CONSTANT` and `GUIDANCE_MAX_FORCE`
  scaled down by 0.75, keeping the same proportional shape rather than changing which of the two
  dominates.
- **Mouse-button forwarding wasn't actually suspended during the buttons tutorial** ("we also need
  to make sure button controls aren't mapped to the mouse during the buttons tutorial - right now
  you can be silently clicking things on programs during this step"). `mouseSuspended` already
  covered Stage 0/1 and already gated cursor movement and rear-button/scroll-wheel forwarding, but
  the left-click (front/select button) and quickslot-cycle (scroll-click) forwarding for the real
  active-slot room was never gated by it at all - that branch is reached purely by room/zone state
  (`isControllingActiveRoom` and friends), and nothing there re-checked `mouseSuspended` before
  acting. A user physically standing in the active room's Z-band during Stage 1's button tour
  (which doesn't move them anywhere, so this was easy to hit) had every "press select now"
  checkpoint also silently left-click whatever program was showing there. Fixed by wrapping that
  branch's forwarding in the same `!mouseSuspended` check the other paths already use.
- **Stage 5's dictation/AI-assistant prompts reworded to make the choice explicit** ("make it
  clear that the user will have the choice to set up dictation / AI assist or not after the hold
  aux - right now it could feel like it's pulling you into setting it up without an exit path").
  The exit path already existed - holding AUX opens `SetupConfirmationPrompt`'s two-detent
  confirm/cancel prompt, which defaults to Cancel - but the tutorial's own line ("...and I'll walk
  you through it") read like a commitment rather than an invitation to choose. Both the 5b and 5d
  checkpoint lines (and their repeat reminders) now say "...and I'll bring up the choice - you can
  go ahead with it, or decide not to, right there."

## Hardware-test fixes (round 10)

- **Narrate moved off the rear button onto a standalone AUX tap** ("tap to narrate, hold to
  dictate, tap+hold for the AI assistant... keep the back button to right click"). The rear
  button used to do both Narrate (unconditionally) and forward as an OS right-click
  (while controlling an active room) on the same press — this remap cleanly separates the two:
  Narrate now lives entirely on AUX, and the rear button is exclusively right-click. Stage 1's
  button-tour script updated to match (see above). `VoiceAssistant`'s tap-then-hold detection
  window (`TAP_TO_HOLD_WINDOW`) also shortened from 500ms to 250ms, timed from the tap's release
  (not the original press), so Narrate doesn't wait as long to confirm a tap had no follow-up
  hold before speaking.

## Future work ideas (not in scope for v1)

- A way to jump directly to a specific stage (e.g., re-learn just the Program Tray) rather
  than always restarting from Stage 0.
- Per-stage completion tracking, so re-launching the tutorial could offer "resume" vs. "start
  over" (superseded for now by the "always restart from 0" decision above, but worth keeping in
  mind if that ever changes).
- Covering quickslot *count* customization (the horizontal slider in Quick Slots settings) — not
  mentioned anywhere in the current draft.
- Tutorial coverage for scroll-forward/backward, once implemented in FFUI itself (see "Future
  functionality, deferred" above) — Narrate itself is now covered, per round 10 above.
- Stage 4's "3 dummy programs" for an empty Program Tray (see "Implementation notes (v1)" above)
  — needs a proper look at `ObjectsFactory.cpp`'s per-scan-cycle window synthesis first.

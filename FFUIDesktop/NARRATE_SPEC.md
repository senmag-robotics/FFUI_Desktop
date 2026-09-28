# FFUI Narrate — Design Spec

Status: **draft for review, round 1** — all of Gareth's decisions so far folded in below.
Not yet implemented. Written once the foundational behavior questions converged, mirroring how
`TUTORIAL_SPEC.md` was used as a living design doc before that feature was built.

## What this is

A standalone tap of the AUX button ("Narrate") becomes a screen-reader-style tool: tap it to hear what's
currently under attention, hold it to browse a full navigable tree of whatever's on screen —
FFUI's own rooms, or any third-party window. It's explicitly modeled on desktop screen readers
(NVDA's discrete "move and announce" convention) rather than touch-screen "spam whatever you
brush past" screen readers, per Gareth's stated preference: *"from my (limited) experience with
them, they seem to spam out whatever you're hovering over? Personally I find this a bit
over-stimulating."* That preference shapes several decisions below, and (see "Open risk" at the
end) needs to keep shaping the auto-announce feature too, since that one pulls in the opposite
direction if we're not careful.

## Core architecture: one interface, two backends

Narrate needs to walk a tree of "things with a name, a role, a state, children, and neighbors" —
and that description fits both FFUI's own objects (tiles, Program Tray entries, quickslots, menu
items) and a third-party window's UI Automation tree equally well. Rather than building two
parallel narrator implementations, both are wrapped behind one small interface:

```cpp
struct INarratableElement {
    virtual std::wstring name() const = 0;
    virtual std::wstring role() const = 0;      // "button", "text field", "checkbox", ...
    virtual std::wstring state() const = 0;      // "", "checked", "disabled", ...
    virtual std::wstring textContent() const = 0; // current line, for the line-bounding rule below
    virtual std::shared_ptr<INarratableElement> parent() const = 0;
    virtual std::shared_ptr<INarratableElement> firstChild() const = 0;
    virtual std::shared_ptr<INarratableElement> nextSibling() const = 0;
    virtual std::shared_ptr<INarratableElement> previousSibling() const = 0;
    virtual void invoke() const = 0; // "activate" — click a button, toggle a checkbox, etc.
};
```

- **`FfuiNarratableElement`**: backed by FFUI's own object model — tiles, tray entries,
  quickslots, menu items already known to `WindowManager`/`MenuSystem`. This is what Narrate
  walks while an FFUI room (Program Slot, Program Tray, Settings) is what's foreground.
- **`UiaNarratableElement`**: backed by `IUIAutomationElement`, via `ElementFromPoint` (point-and
  -read) or `TreeWalker` (`RawViewWalker`, most likely — least likely to hide anything, at the
  cost of a noisier tree than `ContentViewWalker`; worth comparing both on real hardware once
  built) for navigation. This is what Narrate walks whenever a third-party window is foreground.

Which backend is active is decided purely by whichever window currently has focus — FFUI's own
render surface, or someone else's window — so the same five nav gestures (below) work
identically in both contexts without the user needing to think about which "mode" they're in.

## Trigger model

> **⚠ Needs rethinking (not yet resolved):** this section was written when Narrate was still its
> own physical button (the rear button), free to have its own independent tap/hold distinction.
> Narrate has since moved onto AUX's "plain, standalone tap" case instead - "tap to narrate, hold
> to dictate, tap+hold for the AI assistant" - which means AUX's hold and tap-then-hold gestures
> are both already spoken for (Dictate and the AI assistant, respectively) before Narrate's own
> "Hold Narrate enters navigation mode" idea below even gets a turn. The tap-to-read behavior
> (v1, already built) still works exactly as described. The hold-to-navigate mode below (v1 was
> never built) needs a different trigger before it can be built - a double-tap, a different
> physical button, or something else - since AUX has no spare "hold" left to give it. Left as
> written below for now, as a record of the navigation-gesture design itself (which is still
> sound), not as a still-valid trigger proposal.

- **Tap Narrate** (press-and-release, not currently in navigation mode): read the current
  element right now — whatever's under the real OS cursor if a third-party window is foreground,
  or whatever FFUI object currently has attention if an FFUI room is foreground. Does **not**
  enter navigation mode. This is the "press to read" behavior from Gareth's original framing,
  and stays a single well-defined action for v1 (no held-continuous-sweep variant yet).
- **Hold Narrate** (past the same tap/hold threshold `VoiceAssistant` already uses for its own
  AUX gesture — `TAP_MAX`): enter **navigation mode**, rooted at whatever element the tap-read
  above would have picked. Speaks that starting element, then waits for navigation input.
- **Hold Narrate again, while already in navigation mode**: exit navigation mode entirely,
  returning to normal control (mouse/quickslot forwarding resumes for FFUI rooms; nothing
  stylus-side changes for third-party windows either way).

## Navigation gestures (while in navigation mode)

| Input | Action |
|---|---|
| Scroll lever | Move to next / previous sibling, speak it |
| Select (front button) | Descend into current element's first child, speak it (no-op + short "no children" cue if it has none) |
| Tap Narrate | Move out to parent, speak it (no-op + cue if already at the root) |
| Scroll-lever click (middle button) | Invoke the current element — click a button, toggle a checkbox, etc. |
| AUX | Unaffected — still tap-for-assistant / hold-for-dictate regardless of navigation mode |

Quickslot cycling (the scroll-lever click's normal meaning) isn't meaningful while inside
navigation mode, so reusing that same physical input for "activate" here doesn't collide with
anything — navigation mode fully owns scroll-lever-click's meaning for its duration, the same way
`menuActive`/`isGrabbingWindow` already fully own the button semantics during their own states.

## Content conventions

- **Order**: Name, then Role, then State — e.g. *"Submit, button"*, *"Remember me, checkbox,
  checked"*, *"Search, text field, editing"* — matching the convention used across NVDA/JAWS/
  VoiceOver.
- **Bounding long text**: one Narrate read = the current line (NVDA's default review-cursor
  granularity), not the whole field and not just the current sentence. For a UIA text element,
  this maps directly onto `TextUnit_Line` via `ITextRangeProvider`; for FFUI's own text (if any
  ever needs it), the equivalent is whatever FFUI already treats as a visual line.
- **Repeated presses on the same element**: cut off whatever's currently speaking and restart
  from the top of that same announcement, rather than queuing or ignoring the press.

## Voice

A new, dedicated third global voice, `pNarrateVoice` — mirrors exactly why `pAssistantVoice` was
originally split off from `pSapiVoice` (avoid clipping between independent narration sources).
`SetPriority(SPVPRI_OVER)`, same as the other two. Because `SPVPRI_OVER` voices play over each
other rather than serializing, Narrate speaking "always wins immediately" over the AI assistant
(Gareth's answer: *"Narrate always speaks immediately"*) falls out for free — Narrate just calls
`pNarrateVoice->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL)` unconditionally, with no
awareness of what `pAssistantVoice` or `pSapiVoice` happen to be doing at that moment, and no new
coordination logic needed anywhere.

## Auto-announce (state changes without a Narrate press)

Gareth's answer: state changes should be announced automatically even without pressing Narrate
(*"Yes, announce automatically"*), extended to cover third-party content too, not just FFUI's own
objects.

- **FFUI's own objects**: straightforward — FFUI already knows the instant one of its own tiles,
  toggles, or menu items changes state, so it can call `SpeakFfuiNarration`-style output (through
  `pNarrateVoice`, not `pSapiVoice`, to keep this feature's output on its own dedicated voice)
  directly at the point of change.
- **Third-party content**: subscribe to UI Automation's property-changed / focus-changed events
  (`IUIAutomation::AddPropertyChangedEventHandler`, `AddAutomationFocusChangedEventHandler`) on
  whichever window currently has focus, re-subscribing whenever the foreground window changes and
  unsubscribing from the previous one so handlers don't pile up. Best-effort by nature — some
  apps expose an incomplete or non-conformant UIA tree and simply won't fire these events, which
  fails silently (consistent with `CursorStateHaptics`'s own "custom cursor falls through to
  None" tone) rather than being treated as an error.

**Open risk, flagged rather than resolved**: this is the one place where the design risks
reintroducing exactly the "spam out whatever you're near" over-stimulation Gareth explicitly
built this whole feature to avoid. A real third-party window can fire UIA property-changed events
very frequently — a progress bar advancing, a text caret moving, a live-updating list — and
auto-announcing all of it would be worse than the touchscreen screen readers he was reacting
against. Proposed default, for review rather than already decided: only auto-announce discrete,
meaningful state transitions — `ToggleState`, `SelectionItem` selection, `ExpandCollapseState`,
focus changes — and explicitly exclude continuous/high-frequency property streams (`Value`
changes on something actively being dragged or typed into, caret/text-selection changes). This
would need real tuning once tried against real third-party apps, the same way every haptic
constant in this codebase gets tuned by feel rather than decided on paper.

## Haptics — unrelated change, riding along on the same UIA plumbing

`CursorStateHaptics`'s always-on, continuous cursor-driven buzz is untouched and stays fully
decoupled from Narrate — it doesn't get gated behind Narrate presses, doesn't share state with
navigation mode, nothing. The one change: today's single "Clickable" state (detected purely from
the `IDC_HAND` cursor shape) can't distinguish a real button from a plain hyperlink, since both
commonly show a hand cursor. Since UI Automation is coming into the codebase for Narrate anyway,
`CursorStateHaptics::classifyCurrentCursor()` gains one extra step: **only** in the frames where
the cursor is already hand-shaped (so no added cost on the large majority of frames where it
isn't), do one `ElementFromPoint` lookup and check `ControlType`. `Button`, `MenuItem`,
`ListItem`, `TabItem`, `CheckBox`, and `RadioButton` map to a new, more emphatic
`ClickableControl` vibration; `Hyperlink` and plain text keep today's existing `Clickable`
texture unchanged. Exact vibration parameters for the new state get tuned on hardware like the
other four already are.

## Third-party UIA scope, stated plainly

This is a large addition — full tree navigation (not just point-and-read) across arbitrary
third-party windows, plus live state-change announcements — closer to a second, complementary
screen reader than a small utility. Concretely in scope for v1 per the decisions above:

- `ElementFromPoint`/`GetFocusedElement` for the tap-to-read and navigation-mode-entry point.
- A `TreeWalker`-based parent/child/sibling walk for full navigation.
- Property-changed/focus-changed event subscriptions for auto-announce, scoped to the
  currently-focused window.

Explicitly not attempted: fixing apps with broken/incomplete UIA trees (falls through silently,
matching this codebase's established best-effort tone elsewhere), and any bridging for
non-UIA-conformant legacy apps that would need an MSAA fallback — flagged here as a known gap
rather than solved, since nothing so far has required it.

## Not yet decided

- Exact wording for "no children"/"already at root" navigation-boundary cues.
- Whether Narrate gets its own entry in FFUI Settings (a verbosity toggle, a way to disable
  auto-announce, etc.) — the existing "Settings: FFUI Narrator" tile governs `pSapiVoice`, not
  this new voice, so a "Settings: Narrate" tile is a natural but currently unplanned addition.
- Final filter list for which UIA property-changed events qualify for auto-announce (see "Open
  risk" above) — needs real hardware trial against real third-party apps, not paper reasoning.

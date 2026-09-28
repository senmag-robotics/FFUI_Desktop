#pragma once

// Single, central place to tune the "dead-zone + effective-stiffness" tile detent feel shared
// by every synthetic (non-scanned) tile in the app - FFUI Settings tiles, every MenuSystem menu
// item, and (as of this round) Program Tray tiles - "is there a global setting somewhere I can
// change [the detents]?", per the request.
//
// Before this file existed, each of GridTileObject.cpp (FFUI Settings + MenuSystem tiles) and
// WindowWallObject.cpp (Program Tray) computed this same detent shape from its own separately
// hardcoded numbers. The DEADZONE/GAIN/FLOOR shape constants had already been copied identically
// between them, but the underlying BASE_STIFFNESS/FORCE_LIMIT they were fed had NOT - Program
// Tray tiles were still being constructed with UIElementType::Window's own haptic properties
// (0.0015f stiffness, 0.006f force limit - tuned for a completely different interaction, the
// active-slot front wall you physically push through), nearly double FFUI Settings/MenuSystem's
// 0.0008f/0.0025f. That mismatch, not the shape constants, is why Program Tray tiles felt
// noticeably harder than the menu tiles they were meant to now match - "the program tiles are a
// bit better there, but the detents are a bit too hard now", per the request.
//
// Every value here matches what FFUI Settings/MenuSystem tiles were already using (the feel
// nobody has reported as wrong) - Program Tray is what's being pulled into line with them, not
// the other way around. GridTileObject's own per-instance members (see GridTileObject.h) still
// default from these, so a future instance could still override just one tile's feel if ever
// needed - but touching the values below now retunes every tile-style detent in the app at once.
namespace DetentTuning {
    // The base stiffness and force cap fed into the effective-stiffness formula below.
    constexpr float BASE_STIFFNESS = 0.0008f;
    constexpr float FORCE_LIMIT = 0.0025f;

    // Fraction of a tile's own half-extent that must be crossed before the detent starts
    // pulling at all - smaller feels snappier/less "spongey", larger feels softer/more
    // forgiving.
    constexpr float DEADZONE_FRACTION = 0.12f;

    // Multiplies BASE_STIFFNESS before dividing by STABILITY_FLOOR (or the tile's own smaller
    // dimension, whichever is bigger) to get the effective stiffness actually applied - higher
    // feels firmer/snappier.
    constexpr float STIFFNESS_GAIN = 14.0f;

    // A floor on the "smaller dimension" used in that same effective-stiffness formula, so a
    // very small tile doesn't get an absurdly strong detent just from being small.
    constexpr float STABILITY_FLOOR = 15.0f;
}

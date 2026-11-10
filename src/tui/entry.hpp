#ifndef RISC201_TUI_ENTRY_HPP
#define RISC201_TUI_ENTRY_HPP

// ---------------------------------------------------------------------------
// Per-pipeline TUI entry points.
//
// The two latch families cannot share a translation unit, so each pipeline
// gets its own tiny .cpp that owns its CPU instance and forwards to the
// pipeline-agnostic event loop.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

// Opens the TUI debugger for a loaded program.  Returns false when the
// terminal is not interactive, so callers can report a helpful error.
bool runTuiFourStage(const std::vector<std::string>& program);
bool runTuiSixStage(const std::vector<std::string>& program);

#endif // RISC201_TUI_ENTRY_HPP

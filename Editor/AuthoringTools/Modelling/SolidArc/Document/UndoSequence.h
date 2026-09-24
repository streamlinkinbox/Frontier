//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Document/UndoSequence.h — Undo / redo as whole-document snapshots with a cheap change fingerprint
//============================================================================================================================================
// Snapshots are the honest choice while documents are a few hundred poles: no per-command inverse logic to get wrong,
//    and selection state undoes with the geometry (Blender behaviour). A fingerprint over identities, switch and pole
//    bits decides whether a command changed anything, so the console can record every command uniformly.
#pragma once

#include "SceneDocument.h"
#include <deque>
#include <string>

namespace Frontier
{

class UndoSequence
{
public:
    struct Entry
    {
        std::string   Label;                                                            // [-] command text that caused the change
        SceneDocument Before;                                                           // [-] document as it was before that command
    };

    [[nodiscard]] static uint64_t Fingerprint(const SceneDocument& Scene) noexcept;

    // Record a state that a subsequent command may change. Cheap when nothing changes (see Settle).
    void Record(const SceneDocument& Before, std::string Label) noexcept;
    void Relabel(std::string Label) noexcept { PendingEntry.Label = std::move(Label); }
    void Abandon() noexcept { Pending = false; }
    // Called after the command: keeps the pending record only when the fingerprint moved. Returns true when recorded.
    bool Settle(const SceneDocument& After) noexcept;

    [[nodiscard]] bool CanUndo() const noexcept { return !UndoStack.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !RedoStack.empty(); }
    // Swap the current document with the top of the respective stack; returns the label of the step taken.
    [[nodiscard]] std::string Undo(SceneDocument& Current) noexcept;
    [[nodiscard]] std::string Redo(SceneDocument& Current) noexcept;
    [[nodiscard]] const std::deque<Entry>& UndoEntries() const noexcept { return UndoStack; }
    [[nodiscard]] const std::deque<Entry>& RedoEntries() const noexcept { return RedoStack; }
    void Clear() noexcept { UndoStack.clear(); RedoStack.clear(); Pending = false; }
    void Cap(size_t Steps) noexcept { Limit = Steps; }

private:
    std::deque<Entry> UndoStack;
    std::deque<Entry> RedoStack;
    Entry             PendingEntry;
    uint64_t          PendingFingerprint = 0;
    bool              Pending = false;
    size_t            Limit = 200;
};

} // namespace Frontier

// undo_stack.h — Geri al / yinele yığını (anlık görüntü tabanlı).
//
// Her düzenlemeden ÖNCE belgenin tam bir kopyası (sahne ağacı + ışıklar + ortam)
// alınır. Geometri değişmez olduğu için kopya ucuzdur: mesh'ler paylaşılır,
// yalnız malzemeler ve dönüşümler kopyalanır. Komut-tabanlı geri almaya göre
// çok daha az hata yapar: silinen bir düğüme işaret eden komut kalmaz.
#pragma once

#include <cstddef>
#include <deque>
#include <utility>

namespace photon {

template <typename Snapshot>
class UndoStack {
public:
    explicit UndoStack(size_t limit = 64) : m_limit(limit) {}

    /// Düzenleme öncesi durum. Yinele geçmişi silinir.
    void push(Snapshot before) {
        m_undo.push_back(std::move(before));
        if (m_undo.size() > m_limit) m_undo.pop_front();
        m_redo.clear();
    }

    /// @p current: şu anki durum (yinele için saklanır). Dönen değer: geri yüklenecek durum.
    bool undo(Snapshot& current) {
        if (m_undo.empty()) return false;
        m_redo.push_back(std::move(current));
        current = std::move(m_undo.back());
        m_undo.pop_back();
        return true;
    }

    bool redo(Snapshot& current) {
        if (m_redo.empty()) return false;
        m_undo.push_back(std::move(current));
        current = std::move(m_redo.back());
        m_redo.pop_back();
        return true;
    }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    void clear() {
        m_undo.clear();
        m_redo.clear();
    }

private:
    size_t m_limit;
    std::deque<Snapshot> m_undo;
    std::deque<Snapshot> m_redo;
};

} // namespace photon

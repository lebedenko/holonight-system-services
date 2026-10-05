#pragma once

#include "CompositorSnapshot.h"

#include <QSet>

struct wl_output;

inline QList<WindowCommand> foreignToplevelOperations(int version) {
  QList<WindowCommand> result{
      WindowCommand::Activate, WindowCommand::Minimize,   WindowCommand::Restore,
      WindowCommand::Maximize, WindowCommand::Unmaximize, WindowCommand::Close,
  };
  if (version >= 2) {
    result.append({WindowCommand::Fullscreen, WindowCommand::Unfullscreen});
  }
  return result;
}

// Properties are pending until the protocol's per-handle done event.
struct ForeignToplevelState {
  struct Properties {
    CompositorActiveWindow window;
    QSet<wl_output*> outputs;
    bool activated{false};
    bool minimized{false};
    bool maximized{false};
    bool fullscreen{false};
    quint64 order{0};
  } pending, committed;

  void setActivated(bool activated, quint64& order) {
    if (activated && !pending.activated) {
      pending.order = ++order;
    }
    pending.activated = activated;
  }
  bool ready{false};
  void commit() {
    committed = pending;
    ready = true;
  }

  static const ForeignToplevelState* active(const QList<const ForeignToplevelState*>& windows) {
    const ForeignToplevelState* selected = nullptr;
    for (const auto* window : windows) {
      if (window->committed.activated &&
          ((selected == nullptr) || window->committed.order > selected->committed.order)) {
        selected = window;
      }
    }
    return selected;
  }
  [[nodiscard]] QHash<QString, CompositorActiveWindow> onOutputs(const QHash<wl_output*, QString>& names) const {
    QHash<QString, CompositorActiveWindow> result;
    for (auto* output : committed.outputs) {
      const auto name = names.value(output);
      if (!name.isEmpty()) {
        result.insert(name, committed.window);
      }
    }
    return result;
  }
};

inline CompositorSnapshot mergeLabwcSnapshot(const CompositorSnapshot& workspace, bool windows_available,
                                             const QHash<QString, CompositorActiveWindow>& windows) {
  CompositorSnapshot snapshot = workspace.connected ? workspace : CompositorSnapshot{};
  snapshot.connected |= windows_available;
  snapshot.capabilities.active_window = windows_available;
  snapshot.active_windows = windows_available ? windows : QHash<QString, CompositorActiveWindow>{};
  if (snapshot.connected) {
    snapshot.diagnostic.clear();
  } else {
    snapshot.diagnostic = QStringLiteral("labwc workspace and foreign-toplevel protocols are unavailable");
  }
  return snapshot;
}

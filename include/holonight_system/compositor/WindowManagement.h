#pragma once

#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <optional>

// NOLINTNEXTLINE(performance-enum-size): Preserve plugin ABI.
enum class WindowCommand { Activate, Minimize, Restore, Maximize, Unmaximize, Fullscreen, Unfullscreen, Close };
// NOLINTNEXTLINE(performance-enum-size): Preserve plugin ABI.
enum class WindowCommandResult { Accepted, InvalidWindow, Unsupported, Disconnected, MissingSeat, Busy, Failed };

struct CompositorWindow {
  QString id;
  QString title;
  QString app_id;
  QStringList outputs;
  bool activated{false};
  bool minimized{false};
  bool maximized{false};
  bool fullscreen{false};
  QList<WindowCommand> operations;
  quint32 pid{0};
  std::optional<QString> workspace_id;
  std::optional<QRectF> layout_rect;
  std::optional<QString> foreign_toplevel_identifier;
  bool operator==(const CompositorWindow&) const = default;
};

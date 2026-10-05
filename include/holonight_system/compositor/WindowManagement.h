#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// NOLINTNEXTLINE(performance-enum-size): Preserve plugin ABI.
enum class WindowCommand { Activate, Minimize, Restore, Maximize, Unmaximize, Fullscreen, Unfullscreen, Close };
// NOLINTNEXTLINE(performance-enum-size): Preserve plugin ABI.
enum class WindowCommandResult { Accepted, InvalidWindow, Unsupported, Disconnected, MissingSeat };

enum class ExternalTitleBarState : quint8 { Unknown, Present, Absent };

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
  ExternalTitleBarState external_title_bar{ExternalTitleBarState::Unknown};
  bool operator==(const CompositorWindow&) const = default;
};

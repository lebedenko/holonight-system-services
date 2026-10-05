#pragma once

#include "WindowManagement.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct CompositorCapabilities {
  bool window_listing{false};
  bool workspace_listing{false};
  bool workspace_activation{false};
  bool window_activation{false};
  bool active_window{false};
  bool focused_output{false};
  bool urgency{false};
  bool occupancy{false};

  bool operator==(const CompositorCapabilities&) const = default;
};

struct CompositorWorkspace {
  QString id;
  QString display_name;
  int stable_order{0};
  QStringList groups;
  QStringList outputs;
  bool active{false};
  bool focused{false};
  bool urgent{false};
  std::optional<bool> occupied;
  bool can_activate{true};

  bool operator==(const CompositorWorkspace&) const = default;
};

struct CompositorActiveWindow {
  QString app_id;
  QString title;
  QString category;

  bool operator==(const CompositorActiveWindow&) const = default;
};

struct CompositorSnapshot {
  bool connected{false};
  QString diagnostic;
  QString focused_output;
  CompositorCapabilities capabilities;
  QList<CompositorWorkspace> workspaces;
  QList<CompositorWindow> windows;
  QHash<QString, CompositorActiveWindow> active_windows;
  QHash<QString, bool> occupied_outputs;
};

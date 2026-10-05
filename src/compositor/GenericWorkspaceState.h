#pragma once
#include "CompositorSnapshot.h"

// Pending protocol state becomes visible only at the manager's done event.
struct GenericWorkspaceState {
  explicit GenericWorkspaceState(int order) {
    workspace.id = QStringLiteral("handle:%1").arg(order);
    workspace.stable_order = order;
    workspace.can_activate = false;
  }
  void setId(const QString& identifier) {
    if (!identifier.isEmpty()) {
      workspace.id = QStringLiteral("protocol:") + identifier;
    }
  }
  void setName(const QString& name) { workspace.display_name = name; }
  void setState(uint32_t state) {
    workspace.active = (state & 1U) != 0;
    workspace.urgent = (state & 2U) != 0;
    hidden = (state & 4U) != 0;
  }
  void setCapabilities(uint32_t capabilities) { workspace.can_activate = (capabilities & 1U) != 0; }
  CompositorWorkspace workspace;
  bool hidden{false};
};

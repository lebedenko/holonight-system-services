#pragma once
#include <QHash>
#include <QString>

struct NumberedWorkspaceState {
  bool eligible{false};
  QHash<QString, int> assignments;
};

// Optional interface. IDs are opaque; only the integration assigns slot numbers.
class NumberedWorkspaceProvider {
 public:
  NumberedWorkspaceProvider() = default;
  NumberedWorkspaceProvider(const NumberedWorkspaceProvider&) = default;
  NumberedWorkspaceProvider& operator=(const NumberedWorkspaceProvider&) = default;
  NumberedWorkspaceProvider(NumberedWorkspaceProvider&&) = default;
  NumberedWorkspaceProvider& operator=(NumberedWorkspaceProvider&&) = default;
  virtual ~NumberedWorkspaceProvider() = default;
  [[nodiscard]] virtual NumberedWorkspaceState numberedWorkspaces() const = 0;
  virtual void activateNumberedSlot(int slot) = 0;
};

#pragma once
#include <QString>
#include <QVariantList>

// Optional shell contribution contract; no QML registration or application models.
class SpecialWorkspaceProvider {
 public:
  SpecialWorkspaceProvider() = default;
  SpecialWorkspaceProvider(const SpecialWorkspaceProvider&) = default;
  SpecialWorkspaceProvider& operator=(const SpecialWorkspaceProvider&) = default;
  SpecialWorkspaceProvider(SpecialWorkspaceProvider&&) = default;
  SpecialWorkspaceProvider& operator=(SpecialWorkspaceProvider&&) = default;
  virtual ~SpecialWorkspaceProvider() = default;
  [[nodiscard]] virtual QVariantList specialWorkspaces() const = 0;
  virtual void activateSpecialWorkspace(const QString& identifier) = 0;
};

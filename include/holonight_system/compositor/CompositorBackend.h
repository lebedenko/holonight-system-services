#pragma once

#include "CompositorSnapshot.h"
#include "WindowActivation.h"

#include <QObject>

class CompositorBackend : public QObject {
  Q_OBJECT

 public:
  using QObject::QObject;
  ~CompositorBackend() override = default;
  CompositorBackend(const CompositorBackend&) = delete;
  CompositorBackend& operator=(const CompositorBackend&) = delete;
  CompositorBackend(CompositorBackend&&) = delete;
  CompositorBackend& operator=(CompositorBackend&&) = delete;

  virtual WindowCommandResult requestWindowCommand(const QString& identifier, WindowCommand command) {
    Q_UNUSED(identifier)
    Q_UNUSED(command)
    return WindowCommandResult::Unsupported;
  }
  virtual void start() = 0;
  virtual void requestSnapshotRefresh() {}
  virtual void activateWorkspace(const QString& workspace_id) = 0;
  virtual WindowActivationResult requestWindowActivation(const WindowActivationRequest& request) {
    Q_UNUSED(request)
    return WindowActivationResult::Unsupported;
  }

 Q_SIGNALS:
  void snapshotReady(CompositorSnapshot _t1);
};

#pragma once

#include "GenericBackend.h"
#include "qwayland-wlr-foreign-toplevel-management-unstable-v1.h"

class LabwcProtocol;
class LabwcWindow;

class LabwcBackend final : public CompositorBackend {
  Q_OBJECT
 public:
  LabwcBackend(const LabwcBackend&) = delete;
  LabwcBackend& operator=(const LabwcBackend&) = delete;
  LabwcBackend(LabwcBackend&&) = delete;
  LabwcBackend& operator=(LabwcBackend&&) = delete;
  explicit LabwcBackend(QObject* parent = nullptr, int maximum_protocol_version = 3);
  ~LabwcBackend() override;
  void start() override;
  WindowCommandResult requestWindowCommand(const QString& identifier, WindowCommand command) override;
  void activateWorkspace(const QString& identifier) override;

 private:
  friend class LabwcProtocol;
  friend class LabwcWindow;
  void connectProtocol();
  void protocolFinished();
  void schedulePublish();
  void publish();
  int maximum_protocol_version_;
  GenericBackend workspace_;
  CompositorSnapshot workspace_snapshot_;
  std::unique_ptr<LabwcProtocol> protocol_;
  QList<LabwcWindow*> windows_;
  QTimer reconnect_timer_;
  quint64 activation_order_{0};
  bool available_{false};
  bool publish_pending_{false};
};

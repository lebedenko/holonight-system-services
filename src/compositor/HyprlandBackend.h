#pragma once

#include "CompositorBackend.h"
#include "HyprlandIpcClient.h"
#include "NumberedWorkspaceProvider.h"
#include "SpecialWorkspaceProvider.h"

#include <QSet>
#include <QVariantList>

#include <cstdint>

class HyprlandBackend final : public CompositorBackend,
                              public NumberedWorkspaceProvider,
                              public SpecialWorkspaceProvider {
  Q_OBJECT

 public:
  explicit HyprlandBackend(HyprlandIpcTransportPtr transport = {}, QObject* parent = nullptr);
  void start() override;
  [[nodiscard]] NumberedWorkspaceState numberedWorkspaces() const override { return numbered_; }
  void activateNumberedSlot(int slot) override;
  [[nodiscard]] QVariantList specialWorkspaces() const override { return special_workspaces_; }
  void activateSpecialWorkspace(const QString& identifier) override;
 Q_SIGNALS:
  void specialWorkspacesChanged();

 public:
  void activateWorkspace(const QString& workspace_id) override;
  WindowCommandResult requestWindowCommand(const QString& identifier, WindowCommand command) override;
  [[nodiscard]] WindowActivationResult requestWindowActivation(const WindowActivationRequest& request) override;

 private:
  enum class Phase : std::uint8_t {
    Idle,
    Monitors,
    Workspaces,
    Clients,
    WorkspaceActivation,
    LuaActivation,
    WindowActivation,
    LuaWindowActivation,
  };
  void scheduleRefresh();
  void beginRefresh();
  void handleCommand(const QByteArray& response, bool success);
  void handleCommandFailure(Phase failed_phase);
  void handleEvent(const QByteArray& line);
  void runLuaActivation();
  void runLuaWindowActivation();
  void drainWork();
  bool beginWindowActivation(const QString& address);
  void publishClients(const QByteArray& clients_json);
  void fail(const QString& diagnostic);

  void dispatchWorkspace(const QString& workspace_id);
  NumberedWorkspaceState numbered_;
  QVariantList special_workspaces_;
  HyprlandIpcTransportPtr transport_;
  Phase phase_{Phase::Idle};
  QByteArray monitors_;
  QByteArray workspaces_;
  QString activation_id_;
  QString window_activation_address_;
  bool activation_is_special_{false};
  QString pending_activation_;
  WindowCommandResult activateWindow(const QString& address, bool revalidate);
  QSet<QString> window_ids_;
  QHash<QString, QString> workspace_selectors_;
  QList<WindowActivationCandidate> activation_candidates_;
  QList<QString> activation_addresses_;
  struct PendingWindow {
    QString id;
    bool revalidate;
  };
  std::optional<PendingWindow> pending_window_address_;
  QSet<QString> urgent_addresses_;
  bool refresh_dirty_{false};
  bool refresh_scheduled_{false};
};

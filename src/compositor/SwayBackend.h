#pragma once

#include "CompositorBackend.h"
#include "SwayIpc.h"

#include <QLocalSocket>
#include <QTimer>

#include <cstdint>
#include <optional>

class SwayBackend final : public CompositorBackend, public NumberedWorkspaceProvider {
  Q_OBJECT

 public:
  explicit SwayBackend(QString socket_path = {}, QObject* parent = nullptr);
  void start() override;
  void requestSnapshotRefresh() override { scheduleRefresh(); }
  [[nodiscard]] NumberedWorkspaceState numberedWorkspaces() const override { return numbered_; }
  void activateNumberedSlot(int slot) override;
  void activateWorkspace(const QString& workspace_id) override;
  [[nodiscard]] WindowActivationResult requestWindowActivation(const WindowActivationRequest& request) override;

 private:
  enum class RequestPhase : std::uint8_t { Idle, Workspaces, Outputs, Tree, WorkspaceActivation, WindowActivation };
  static constexpr quint32 kGetWorkspaces = 1;
  static constexpr quint32 kSubscribe = 2;
  static constexpr quint32 kGetOutputs = 3;
  static constexpr quint32 kGetTree = 4;
  static constexpr quint32 kCommand = 0;
  static constexpr quint32 kEventBit = 1U << 31U;

  void connectSockets();
  [[nodiscard]] bool sendRequest(quint32 type, const QByteArray& payload = {});
  void handleRequestData();
  void handleSubscriptionData();
  [[nodiscard]] quint32 expectedResponseType() const;
  bool handleRequestFrame(const SwayIpcFrame& frame);
  void finishRefresh(const QByteArray& tree);
  void finishActivation(const QByteArray& payload, RequestPhase completed_phase);
  void disconnectSession(const QString& diagnostic);
  void scheduleRefresh();
  void beginRefresh();
  void fail(const QString& diagnostic);
  void scheduleReconnect();
  void drainWork();
  bool beginWindowActivation(quint64 container_id);

  struct WorkspaceActivation {
    QString id;
    int slot{0};
  };
  void dispatchActivation(const WorkspaceActivation& activation);
  NumberedWorkspaceState numbered_;
  QHash<QString, QString> workspace_names_;
  QString socket_path_;
  QLocalSocket request_socket_;
  QLocalSocket subscription_socket_;
  SwayIpcDecoder request_decoder_;
  SwayIpcDecoder subscription_decoder_;
  QTimer refresh_timer_;
  QTimer reconnect_timer_;
  RequestPhase phase_{RequestPhase::Idle};
  QByteArray workspaces_;
  QByteArray outputs_;
  std::optional<WorkspaceActivation> pending_activation_;
  QList<WindowActivationCandidate> activation_candidates_;
  QList<quint64> activation_container_ids_;
  std::optional<quint64> pending_window_container_id_;
  bool refresh_dirty_{false};
  bool subscription_ready_{false};
  int reconnect_delay_ms_{1000};
};

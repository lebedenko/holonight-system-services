#include "SwayBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <limits>
#include <utility>

SwayBackend::SwayBackend(QString socket_path, QObject* parent)
    : CompositorBackend(parent),
      socket_path_(socket_path.isEmpty() ? qEnvironmentVariable("SWAYSOCK") : std::move(socket_path)) {
  refresh_timer_.setSingleShot(true);
  reconnect_timer_.setSingleShot(true);
  connect(&refresh_timer_, &QTimer::timeout, this, &SwayBackend::beginRefresh);
  connect(&reconnect_timer_, &QTimer::timeout, this, &SwayBackend::connectSockets);
  connect(&request_socket_, &QLocalSocket::readyRead, this, &SwayBackend::handleRequestData);
  connect(&subscription_socket_, &QLocalSocket::readyRead, this, &SwayBackend::handleSubscriptionData);
  connect(&request_socket_, &QLocalSocket::connected, this, &SwayBackend::scheduleRefresh);
  connect(&subscription_socket_, &QLocalSocket::connected, this, [this] {
    subscription_socket_.write(
        encodeSwayIpcFrame(kSubscribe, QByteArrayLiteral(R"(["workspace","window","output","shutdown"])")));
  });
  const auto disconnected = [this] { disconnectSession(QStringLiteral("Sway IPC disconnected")); };
  connect(&request_socket_, &QLocalSocket::disconnected, this, disconnected);
  connect(&subscription_socket_, &QLocalSocket::disconnected, this, disconnected);
}

void SwayBackend::start() {
  if (socket_path_.isEmpty()) {
    fail(QStringLiteral("SWAYSOCK is not set"));
    return;
  }
  connectSockets();
}

void SwayBackend::connectSockets() {
  phase_ = RequestPhase::Idle;
  request_decoder_ = {};
  subscription_decoder_ = {};
  subscription_ready_ = false;
  request_socket_.connectToServer(socket_path_, QIODevice::ReadWrite);
  subscription_socket_.connectToServer(socket_path_, QIODevice::ReadWrite);
}

void SwayBackend::activateWorkspace(const QString& workspace_id) {
  if (workspace_names_.contains(workspace_id)) {
    dispatchActivation({.id = workspace_id});
  }
}
void SwayBackend::activateNumberedSlot(int slot) {
  if (slot > 0) {
    dispatchActivation({.slot = slot});
  }
}
void SwayBackend::dispatchActivation(const WorkspaceActivation& activation) {
  if (request_socket_.state() != QLocalSocket::ConnectedState) {
    return;
  }
  if (phase_ != RequestPhase::Idle) {
    pending_activation_ = activation;
    return;
  }
  if (activation.slot == 0 && !workspace_names_.contains(activation.id)) {
    drainWork();
    return;
  }
  const QString command =
      activation.slot > 0 ? QStringLiteral("workspace --no-auto-back-and-forth number %1").arg(activation.slot)
                          : QStringLiteral("workspace --no-auto-back-and-forth \"") +
                                escapeSwayWorkspaceName(workspace_names_.value(activation.id)) + QStringLiteral("\"");
  phase_ = RequestPhase::WorkspaceActivation;
  if (!sendRequest(kCommand, command.toUtf8())) {
    phase_ = RequestPhase::Idle;
    fail(QStringLiteral("Sway workspace activation transport failed"));
    refresh_dirty_ = true;
    drainWork();
  }
}

bool SwayBackend::sendRequest(quint32 type, const QByteArray& payload) {
  const QByteArray frame = encodeSwayIpcFrame(type, payload);
  return request_socket_.write(frame) == frame.size();
}

void SwayBackend::scheduleRefresh() {
  if (phase_ != RequestPhase::Idle) {
    refresh_dirty_ = true;
    return;
  }
  if (!refresh_timer_.isActive()) {
    refresh_timer_.start(0);
  }
}

void SwayBackend::beginRefresh() {
  if (request_socket_.state() != QLocalSocket::ConnectedState || phase_ != RequestPhase::Idle) {
    return;
  }
  refresh_dirty_ = false;
  workspaces_.clear();
  outputs_.clear();
  phase_ = RequestPhase::Workspaces;
  if (!sendRequest(kGetWorkspaces)) {
    disconnectSession(QStringLiteral("Sway refresh transport failed"));
  }
}

void SwayBackend::handleRequestData() {
  if (!request_decoder_.append(request_socket_.readAll())) {
    disconnectSession(request_decoder_.error());
    return;
  }
  for (const SwayIpcFrame& frame : request_decoder_.takeFrames()) {
    if (!handleRequestFrame(frame)) {
      return;
    }
  }
}

quint32 SwayBackend::expectedResponseType() const {
  switch (phase_) {
    case RequestPhase::Workspaces:
      return kGetWorkspaces;
    case RequestPhase::Outputs:
      return kGetOutputs;
    case RequestPhase::Tree:
      return kGetTree;
    case RequestPhase::WorkspaceActivation:
    case RequestPhase::WindowActivation:
      return kCommand;
    case RequestPhase::Idle:
      return std::numeric_limits<quint32>::max();
  }
  return std::numeric_limits<quint32>::max();
}

bool SwayBackend::handleRequestFrame(const SwayIpcFrame& frame) {
  if (frame.type != expectedResponseType()) {
    disconnectSession(QStringLiteral("unexpected Sway IPC response type"));
    return false;
  }
  switch (phase_) {
    case RequestPhase::Workspaces:
      workspaces_ = frame.payload;
      phase_ = RequestPhase::Outputs;
      if (!sendRequest(kGetOutputs)) {
        disconnectSession(QStringLiteral("Sway refresh transport failed"));
        return false;
      }
      break;
    case RequestPhase::Outputs:
      outputs_ = frame.payload;
      phase_ = RequestPhase::Tree;
      if (!sendRequest(kGetTree)) {
        disconnectSession(QStringLiteral("Sway refresh transport failed"));
        return false;
      }
      break;
    case RequestPhase::Tree:
      finishRefresh(frame.payload);
      break;
    case RequestPhase::WorkspaceActivation:
    case RequestPhase::WindowActivation:
      finishActivation(frame.payload, phase_);
      break;
    case RequestPhase::Idle:
      return false;
  }
  return true;
}

void SwayBackend::finishRefresh(const QByteArray& tree) {
  phase_ = RequestPhase::Idle;
  if (auto refresh = parseSwayRefresh(workspaces_, outputs_, tree)) {
    reconnect_delay_ms_ = 1000;
    numbered_ = refresh->numbered;
    workspace_names_ = refresh->names;
    activation_candidates_.clear();
    activation_container_ids_.clear();
    for (const SwayWindowInfo& window : refresh->windows) {
      activation_candidates_.append(window.candidate);
      activation_container_ids_.append(window.container_id);
    }
    refresh->snapshot.capabilities.window_activation = true;
    emit snapshotReady(std::move(refresh->snapshot));
  } else {
    fail(QStringLiteral("invalid Sway refresh response"));
  }
  drainWork();
}

void SwayBackend::finishActivation(const QByteArray& payload, RequestPhase completed_phase) {
  phase_ = RequestPhase::Idle;
  const QJsonDocument response = QJsonDocument::fromJson(payload);
  if (!response.isArray() || response.array().isEmpty() ||
      !response.array().first().toObject().value(QStringLiteral("success")).toBool(false)) {
    fail(completed_phase == RequestPhase::WindowActivation ? QStringLiteral("Sway window activation rejected")
                                                           : QStringLiteral("Sway workspace activation failed"));
  }
  refresh_dirty_ = true;
  drainWork();
}

WindowActivationResult SwayBackend::requestWindowActivation(const WindowActivationRequest& request) {
  if (!isValidWindowActivationRequest(request)) {
    return WindowActivationResult::InvalidRequest;
  }
  if (request_socket_.state() != QLocalSocket::ConnectedState) {
    return WindowActivationResult::Disconnected;
  }
  const WindowActivationResolution resolution = resolveWindowActivation(request, activation_candidates_);
  if (resolution.result != WindowActivationResult::Accepted) {
    return resolution.result;
  }
  const quint64 container_id = activation_container_ids_.at(*resolution.candidate_index);
  if (phase_ != RequestPhase::Idle) {
    if (pending_window_container_id_) {
      return WindowActivationResult::Busy;
    }
    pending_window_container_id_ = container_id;
    return WindowActivationResult::Accepted;
  }
  if (!beginWindowActivation(container_id)) {
    fail(QStringLiteral("Sway window activation transport failed"));
    scheduleRefresh();
    return WindowActivationResult::Failed;
  }
  return WindowActivationResult::Accepted;
}

bool SwayBackend::beginWindowActivation(quint64 container_id) {
  phase_ = RequestPhase::WindowActivation;
  const QByteArray command =
      QByteArrayLiteral("[con_id=") + QByteArray::number(container_id) + QByteArrayLiteral("] focus");
  if (sendRequest(kCommand, command)) {
    return true;
  }
  phase_ = RequestPhase::Idle;
  return false;
}

void SwayBackend::drainWork() {
  if (phase_ != RequestPhase::Idle) {
    return;
  }
  if (pending_window_container_id_) {
    const quint64 container_id = std::exchange(pending_window_container_id_, std::nullopt).value();
    if (!beginWindowActivation(container_id)) {
      fail(QStringLiteral("Sway window activation transport failed"));
      refresh_dirty_ = true;
      scheduleRefresh();
    }
    return;
  }
  if (pending_activation_) {
    const auto activation = std::exchange(pending_activation_, std::nullopt).value();
    dispatchActivation(activation);
    return;
  }
  if (refresh_dirty_) {
    scheduleRefresh();
  }
}

void SwayBackend::handleSubscriptionData() {
  if (!subscription_decoder_.append(subscription_socket_.readAll())) {
    fail(subscription_decoder_.error());
    disconnectSession(subscription_decoder_.error());
    return;
  }
  for (const SwayIpcFrame& frame : subscription_decoder_.takeFrames()) {
    if (!subscription_ready_) {
      const QJsonDocument response = QJsonDocument::fromJson(frame.payload);
      if (frame.type != kSubscribe || !response.isObject() ||
          !response.object().value(QStringLiteral("success")).toBool(false)) {
        fail(QStringLiteral("Sway IPC subscription rejected"));
        disconnectSession(QStringLiteral("Sway IPC subscription rejected"));
        return;
      }
      subscription_ready_ = true;
      continue;
    }
    if ((frame.type & kEventBit) == 0) {
      fail(QStringLiteral("non-event traffic on Sway subscription socket"));
      disconnectSession(QStringLiteral("non-event traffic on Sway subscription socket"));
      return;
    }
    scheduleRefresh();
  }
}

void SwayBackend::fail(const QString& diagnostic) {
  numbered_ = {};
  workspace_names_.clear();
  emit snapshotReady({.connected = false, .diagnostic = diagnostic});
}

void SwayBackend::disconnectSession(const QString& diagnostic) {
  if (request_socket_.state() == QLocalSocket::UnconnectedState &&
      subscription_socket_.state() == QLocalSocket::UnconnectedState && reconnect_timer_.isActive()) {
    return;
  }
  phase_ = RequestPhase::Idle;
  workspaces_.clear();
  outputs_.clear();
  activation_candidates_.clear();
  activation_container_ids_.clear();
  pending_window_container_id_.reset();
  pending_activation_.reset();
  refresh_dirty_ = false;
  fail(diagnostic);
  request_socket_.abort();
  subscription_socket_.abort();
  scheduleReconnect();
}

void SwayBackend::scheduleReconnect() {
  if (reconnect_timer_.isActive()) {
    return;
  }
  reconnect_timer_.start(reconnect_delay_ms_);
  reconnect_delay_ms_ = std::min(reconnect_delay_ms_ * 2, 30000);
}

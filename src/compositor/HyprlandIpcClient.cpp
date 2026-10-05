#include "HyprlandIpcClient.h"

#include <QLoggingCategory>

#include <algorithm>
#include <utility>

Q_LOGGING_CATEGORY(lcHyprlandIpcClient, "holonight.hyprland.ipc")

HyprlandIpcClient::HyprlandIpcClient(QString service_name, QObject* parent)
    : HyprlandIpcTransport(parent), service_name_(std::move(service_name)) {}

HyprlandIpcClient::HyprlandIpcClient(QString service_name, QString event_socket_path, QString command_socket_path,
                                     bool use_abstract_namespace, QObject* parent)
    : HyprlandIpcTransport(parent),
      service_name_(std::move(service_name)),
      event_socket_path_(std::move(event_socket_path)),
      command_socket_path_(std::move(command_socket_path)),
      use_abstract_namespace_(use_abstract_namespace) {}

void HyprlandIpcClient::connectEventStream() {
  if (event_socket_ != nullptr) {
    event_socket_->disconnect(this);
    event_socket_->deleteLater();
    event_socket_ = nullptr;
    connect_timeout_ = nullptr;
  }

  const QString path = resolvedEventSocketPath();
  if (path.isEmpty()) {
    qCWarning(lcHyprlandIpcClient) << service_name_ << "HYPRLAND_INSTANCE_SIGNATURE not set";
    scheduleReconnect();
    return;
  }

  event_socket_ = new QLocalSocket(this);
  if (use_abstract_namespace_) {
    event_socket_->setSocketOptions(QLocalSocket::AbstractNamespaceOption);
  }
  connect(event_socket_, &QLocalSocket::connected, this, &HyprlandIpcClient::onEventSocketConnected);
  connect(event_socket_, &QLocalSocket::readyRead, this, &HyprlandIpcClient::onEventSocketReadable);
  connect(event_socket_, &QLocalSocket::disconnected, this, &HyprlandIpcClient::onEventSocketDisconnected);
  connect(event_socket_, &QLocalSocket::errorOccurred, this, &HyprlandIpcClient::onEventSocketError);
  event_socket_->connectToServer(path, QIODeviceBase::ReadOnly);

  connect_timeout_ = new QTimer(event_socket_);
  connect_timeout_->setSingleShot(true);
  connect(connect_timeout_, &QTimer::timeout, this, [this] {
    if (event_socket_ == nullptr || event_socket_->state() == QLocalSocket::ConnectedState) {
      return;
    }
    qCWarning(lcHyprlandIpcClient) << service_name_
                                   << "event socket connection timed out:" << event_socket_->errorString();
    event_socket_->abort();
    scheduleReconnect();
  });
  connect_timeout_->start(kConnectTimeoutMs);
}

bool HyprlandIpcClient::runCommand(const QByteArray& command, CommandCompletePredicate is_complete) {
  if (command_socket_ != nullptr) {
    return false;
  }

  const QString path = resolvedCommandSocketPath();
  if (path.isEmpty()) {
    qCWarning(lcHyprlandIpcClient) << service_name_ << "HYPRLAND_INSTANCE_SIGNATURE not set; dropping command"
                                   << command;
    return false;
  }

  active_command_ = command;
  command_buffer_.clear();
  command_complete_ = std::move(is_complete);
  command_socket_ = new QLocalSocket(this);
  if (use_abstract_namespace_) {
    command_socket_->setSocketOptions(QLocalSocket::AbstractNamespaceOption);
  }
  connect(command_socket_, &QLocalSocket::connected, this, &HyprlandIpcClient::onCommandSocketConnected);
  connect(command_socket_, &QLocalSocket::readyRead, this, &HyprlandIpcClient::onCommandSocketReadable);
  connect(command_socket_, &QLocalSocket::disconnected, this, &HyprlandIpcClient::onCommandSocketDisconnected);
  connect(command_socket_, &QLocalSocket::errorOccurred, this, &HyprlandIpcClient::onCommandSocketError);
  command_socket_->connectToServer(path, QIODeviceBase::ReadWrite);

  command_timeout_ = new QTimer(command_socket_);
  command_timeout_->setSingleShot(true);
  connect(command_timeout_, &QTimer::timeout, this, [this] {
    qCWarning(lcHyprlandIpcClient) << service_name_ << "command socket timed out";
    finishCommand(false);
  });
  command_timeout_->start(command_timeout_ms_);
  return true;
}

QString HyprlandIpcClient::socketBasePath() {
  const QByteArray runtime_dir = qgetenv("XDG_RUNTIME_DIR");
  if (!runtime_dir.isEmpty()) {
    return QString::fromLocal8Bit(runtime_dir) + QStringLiteral("/hypr/");
  }
  return QStringLiteral("/tmp/hypr/");
}

QString HyprlandIpcClient::eventSocketPath() {
  const QByteArray sig = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (sig.isEmpty()) {
    return {};
  }
  return socketBasePath() + QString::fromLocal8Bit(sig) + QStringLiteral("/.socket2.sock");
}

QString HyprlandIpcClient::commandSocketPath() {
  const QByteArray sig = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
  if (sig.isEmpty()) {
    return {};
  }
  return socketBasePath() + QString::fromLocal8Bit(sig) + QStringLiteral("/.socket.sock");
}

void HyprlandIpcClient::onEventSocketReadable() {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != event_socket_) {
    return;
  }

  const QByteArray chunk = active_socket->read(kEventReadChunkSize);
  if (chunk.isEmpty()) {
    return;
  }

  event_buffer_.append(chunk);
  while (true) {
    const qsizetype newline = event_buffer_.indexOf('\n');
    if (newline < 0) {
      break;
    }
    const QByteArray line = event_buffer_.left(newline);
    event_buffer_.remove(0, static_cast<int>(newline + 1));
    emit eventLineReceived(line);
  }
}

void HyprlandIpcClient::onEventSocketConnected() {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != event_socket_) {
    return;
  }

  if (connect_timeout_ != nullptr) {
    connect_timeout_->stop();
  }
  resetReconnectBackoff();
  emit eventStreamConnected();
}

void HyprlandIpcClient::onEventSocketDisconnected() {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != event_socket_) {
    return;
  }

  qCWarning(lcHyprlandIpcClient) << service_name_ << "event socket disconnected";
  emit eventStreamDisconnected();
  scheduleReconnect();
}

void HyprlandIpcClient::onEventSocketError(QLocalSocket::LocalSocketError /*error*/) {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != event_socket_) {
    return;
  }
  qCWarning(lcHyprlandIpcClient) << service_name_ << "event socket error:" << active_socket->errorString();
  if (active_socket->state() != QLocalSocket::ConnectedState) {
    scheduleReconnect();
  }
}

void HyprlandIpcClient::onCommandSocketConnected() {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != command_socket_) {
    return;
  }
  active_socket->write(active_command_);
  active_socket->flush();
}

void HyprlandIpcClient::onCommandSocketReadable() {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != command_socket_) {
    return;
  }
  command_buffer_.append(active_socket->readAll());
  if (command_complete_ && command_complete_(command_buffer_)) {
    finishCommand(true);
  }
}

void HyprlandIpcClient::onCommandSocketDisconnected() {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != command_socket_) {
    return;
  }

  command_buffer_.append(active_socket->readAll());
  finishCommand(true);
}

void HyprlandIpcClient::onCommandSocketError(QLocalSocket::LocalSocketError error) {
  auto* active_socket = qobject_cast<QLocalSocket*>(sender());
  if (active_socket == nullptr || active_socket != command_socket_) {
    return;
  }
  if (error == QLocalSocket::PeerClosedError) {
    command_buffer_.append(active_socket->readAll());
    finishCommand(true);
    return;
  }
  qCWarning(lcHyprlandIpcClient) << service_name_ << "command socket error:" << active_socket->errorString();
  finishCommand(false);
}

void HyprlandIpcClient::finishCommand(bool success) {
  if (command_timeout_ != nullptr) {
    command_timeout_->stop();
    command_timeout_ = nullptr;
  }

  const QByteArray response = command_buffer_;
  if (command_socket_ != nullptr) {
    command_socket_->disconnect(this);
    command_socket_->deleteLater();
    command_socket_ = nullptr;
  }
  active_command_.clear();
  command_buffer_.clear();
  command_complete_ = {};
  emit commandFinished(response, success);
}

QString HyprlandIpcClient::resolvedEventSocketPath() const {
  if (!event_socket_path_.isEmpty()) {
    return event_socket_path_;
  }
  return eventSocketPath();
}

QString HyprlandIpcClient::resolvedCommandSocketPath() const {
  if (!command_socket_path_.isEmpty()) {
    return command_socket_path_;
  }
  return commandSocketPath();
}

void HyprlandIpcClient::scheduleReconnect() {
  if (reconnect_scheduled_) {
    return;
  }

  reconnect_scheduled_ = true;
  if (reconnect_timer_ == nullptr) {
    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    connect(reconnect_timer_, &QTimer::timeout, this, [this] {
      reconnect_scheduled_ = false;
      connectEventStream();
    });
  }

  reconnect_timer_->start(reconnect_delay_ms_);
  reconnect_delay_ms_ = std::min(reconnect_delay_ms_ * 2, kMaxReconnectDelayMs);
}

void HyprlandIpcClient::resetReconnectBackoff() {
  reconnect_scheduled_ = false;
  reconnect_delay_ms_ = kInitialReconnectDelayMs;
  if (reconnect_timer_ != nullptr) {
    reconnect_timer_->stop();
  }
}

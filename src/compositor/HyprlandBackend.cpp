#include "HyprlandBackend.h"

#include "HyprlandIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace {
bool responseIsError(const QByteArray& response) {
  return response.trimmed().toLower().startsWith(QByteArrayLiteral("error:"));
}

QByteArray escapeLuaString(QString value) {
  value.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
  value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
  return value.toUtf8();
}

bool addressMatches(const QSet<QString>& urgent_addresses, const QString& address) {
  return std::ranges::any_of(
      urgent_addresses, [&address](const QString& urgent) { return address.endsWith(urgent, Qt::CaseInsensitive); });
}

void collectMonitors(const QJsonArray& monitors, CompositorSnapshot* snapshot, QHash<int, QString>* outputs,
                     QSet<int>* active_workspaces) {
  for (const auto value : monitors) {
    const QJsonObject monitor = value.toObject();
    const QString output = monitor.value(QStringLiteral("name")).toString();
    const auto collect_active = [&output, outputs, active_workspaces](const QJsonObject& workspace) {
      const int workspace_id = workspace.value(QStringLiteral("id")).toInt();
      if (!output.isEmpty() && workspace_id != 0) {
        outputs->insert(workspace_id, output);
        active_workspaces->insert(workspace_id);
      }
    };
    collect_active(monitor.value(QStringLiteral("activeWorkspace")).toObject());
    collect_active(monitor.value(QStringLiteral("specialWorkspace")).toObject());
    if (monitor.value(QStringLiteral("focused")).toBool(false)) {
      snapshot->focused_output = output;
    }
  }
}

void collectClients(const QList<HyprlandClientInfo>& clients, QHash<int, int>* window_counts,
                    QHash<int, HyprlandClientInfo>* focused_clients) {
  for (const HyprlandClientInfo& client : clients) {
    if (client.app_class.isEmpty() || client.title.isEmpty()) {
      continue;
    }
    ++(*window_counts)[client.workspace_id];
    if (!focused_clients->contains(client.workspace_id) ||
        client.focus_history_id < focused_clients->value(client.workspace_id).focus_history_id) {
      focused_clients->insert(client.workspace_id, client);
    }
  }
}

void appendWorkspaces(const QJsonArray& workspaces, const QList<HyprlandClientInfo>& clients,
                      const QHash<int, QString>& workspace_outputs, const QSet<int>& active_workspaces,
                      const QHash<int, int>& window_counts, const QHash<int, HyprlandClientInfo>& focused_clients,
                      QSet<QString>* urgent_addresses, CompositorSnapshot* snapshot, NumberedWorkspaceState* numbered,
                      QVariantList* specials) {
  int order = 0;
  for (const auto value : workspaces) {
    const QJsonObject workspace = value.toObject();
    const int workspace_id = workspace.value(QStringLiteral("id")).toInt();
    const QString name = workspace.value(QStringLiteral("name")).toString();
    if (workspace_id == 0 || name.isEmpty()) {
      continue;
    }
    const bool special = workspace_id < 0 || name.startsWith(QStringLiteral("special:"));
    const bool active = active_workspaces.contains(workspace_id);
    const bool urgent =
        !special && std::ranges::any_of(clients, [urgent_addresses, workspace_id](const HyprlandClientInfo& client) {
          return client.workspace_id == workspace_id && addressMatches(*urgent_addresses, client.address);
        });
    if (active) {
      urgent_addresses->removeIf([&clients, workspace_id](const QString& address) {
        return std::ranges::any_of(clients, [&address, workspace_id](const HyprlandClientInfo& client) {
          return client.workspace_id == workspace_id && client.address.endsWith(address, Qt::CaseInsensitive);
        });
      });
    }
    CompositorWorkspace entry{
        .id = special ? name : QString::number(workspace_id),
        .display_name = name,
        .stable_order = order++,
        .outputs = workspace_outputs.contains(workspace_id) ? QStringList{workspace_outputs.value(workspace_id)}
                                                            : QStringList{},
        .active = active,
        .focused = active && workspace_outputs.value(workspace_id) == snapshot->focused_output,
        .urgent = urgent,
        .occupied = window_counts.value(workspace_id) > 0,
    };
    if (active) {
      for (const auto& output : entry.outputs) {
        snapshot->occupied_outputs[output] |= entry.occupied.value_or(false);
      }
    }
    if (special) {
      specials->append(QVariantMap{
          {QStringLiteral("id"), entry.id},
          {QStringLiteral("name"), entry.display_name},
          {QStringLiteral("active"), entry.active},
          {QStringLiteral("urgent"), entry.urgent},
          {QStringLiteral("occupied"), entry.occupied.value_or(false)},
          {QStringLiteral("monitorNames"), entry.outputs},
      });
    } else {
      numbered->assignments.insert(entry.id, workspace_id);
      snapshot->workspaces.append(std::move(entry));
    }
    if (active && focused_clients.contains(workspace_id)) {
      const HyprlandClientInfo& client = focused_clients[workspace_id];
      snapshot->active_windows.insert(workspace_outputs.value(workspace_id),
                                      {.app_id = client.app_class, .title = client.title});
    }
  }
}
}  // namespace

HyprlandBackend::HyprlandBackend(HyprlandIpcTransportPtr transport, QObject* parent)
    : CompositorBackend(parent),
      transport_(transport ? std::move(transport)
                           : std::make_unique<HyprlandIpcClient>(QStringLiteral("CompositorService:"))) {
  connect(this, &CompositorBackend::snapshotReady, this, [this](const CompositorSnapshot& snapshot) {
    if (!snapshot.connected) {
      numbered_ = {};
      special_workspaces_.clear();
      emit specialWorkspacesChanged();
    }
  });
  connect(transport_.get(), &HyprlandIpcTransport::eventStreamConnected, this, &HyprlandBackend::scheduleRefresh);
  connect(transport_.get(), &HyprlandIpcTransport::eventStreamDisconnected, this, [this] {
    pending_activation_.clear();
    pending_window_address_.reset();
    activation_candidates_.clear();
    activation_addresses_.clear();
    urgent_addresses_.clear();
    fail(QStringLiteral("Hyprland IPC disconnected"));
  });
  connect(transport_.get(), &HyprlandIpcTransport::eventLineReceived, this, &HyprlandBackend::handleEvent);
  connect(transport_.get(), &HyprlandIpcTransport::commandFinished, this, &HyprlandBackend::handleCommand);
}

void HyprlandBackend::start() { transport_->connectEventStream(); }

void HyprlandBackend::scheduleRefresh() {
  if (phase_ != Phase::Idle || transport_->hasRunningCommand()) {
    refresh_dirty_ = true;
    return;
  }
  if (refresh_scheduled_) return;
  refresh_scheduled_ = true;
  QTimer::singleShot(0, this, [this] {
    refresh_scheduled_ = false;
    beginRefresh();
  });
}

void HyprlandBackend::beginRefresh() {
  if (phase_ != Phase::Idle || transport_->hasRunningCommand()) {
    refresh_dirty_ = true;
    return;
  }
  refresh_dirty_ = false;
  phase_ = Phase::Monitors;
  if (!transport_->runCommand(QByteArrayLiteral("j/monitors"))) {
    fail(QStringLiteral("Hyprland monitor query failed"));
  }
}

void HyprlandBackend::handleEvent(const QByteArray& line) {
  if (const auto urgent = parseHyprlandUrgentWindowEvent(line)) {
    urgent_addresses_.insert(*urgent);
  }
  scheduleRefresh();
}

void HyprlandBackend::handleCommand(const QByteArray& response, bool success) {
  if (!success) {
    const Phase failed_phase = phase_;
    phase_ = Phase::Idle;
    handleCommandFailure(failed_phase);
    return;
  }
  if (phase_ == Phase::Monitors) {
    monitors_ = response;
    phase_ = Phase::Workspaces;
    if (!transport_->runCommand(QByteArrayLiteral("j/workspaces"))) {
      fail(QStringLiteral("Hyprland workspace query failed"));
    }
  } else if (phase_ == Phase::Workspaces) {
    workspaces_ = response;
    phase_ = Phase::Clients;
    if (!transport_->runCommand(QByteArrayLiteral("j/clients"))) {
      fail(QStringLiteral("Hyprland client query failed"));
    }
  } else if (phase_ == Phase::Clients) {
    phase_ = Phase::Idle;
    publishClients(response);
    drainWork();
  } else if (phase_ == Phase::WorkspaceActivation) {
    if (responseIsError(response)) {
      runLuaActivation();
      return;
    }
    phase_ = Phase::Idle;
    refresh_dirty_ = true;
    drainWork();
  } else if (phase_ == Phase::LuaActivation) {
    phase_ = Phase::Idle;
    if (responseIsError(response)) {
      emit snapshotReady({.diagnostic = QStringLiteral("Hyprland workspace activation failed")});
    }
    refresh_dirty_ = true;
    drainWork();
  } else if (phase_ == Phase::WindowActivation) {
    if (responseIsError(response)) {
      runLuaWindowActivation();
      return;
    }
    phase_ = Phase::Idle;
    refresh_dirty_ = true;
    drainWork();
  } else if (phase_ == Phase::LuaWindowActivation) {
    phase_ = Phase::Idle;
    if (responseIsError(response)) {
      emit snapshotReady({.diagnostic = QStringLiteral("Hyprland window activation rejected")});
    }
    refresh_dirty_ = true;
    drainWork();
  }
}

void HyprlandBackend::handleCommandFailure(Phase failed_phase) {
  if (failed_phase == Phase::WindowActivation || failed_phase == Phase::LuaWindowActivation) {
    emit snapshotReady({.diagnostic = QStringLiteral("Hyprland window activation transport failed")});
    refresh_dirty_ = true;
  } else if (failed_phase == Phase::WorkspaceActivation || failed_phase == Phase::LuaActivation) {
    emit snapshotReady({.diagnostic = QStringLiteral("Hyprland workspace activation failed")});
    refresh_dirty_ = true;
  } else {
    emit snapshotReady({.diagnostic = QStringLiteral("Hyprland snapshot refresh failed")});
  }
  drainWork();
}

void HyprlandBackend::drainWork() {
  if (phase_ != Phase::Idle || transport_->hasRunningCommand()) {
    return;
  }
  if (pending_window_address_) {
    const QString address = std::exchange(pending_window_address_, std::nullopt).value();
    if (!beginWindowActivation(address)) {
      emit snapshotReady({.diagnostic = QStringLiteral("Hyprland window activation transport failed")});
      refresh_dirty_ = true;
      scheduleRefresh();
    }
    return;
  }
  if (!pending_activation_.isEmpty()) {
    const QString activation = std::exchange(pending_activation_, {});
    dispatchWorkspace(activation);
    return;
  }
  if (refresh_dirty_) {
    scheduleRefresh();
  }
}

bool HyprlandBackend::beginWindowActivation(const QString& address) {
  window_activation_address_ = address;
  phase_ = Phase::WindowActivation;
  if (transport_->runCommand(QByteArrayLiteral("dispatch focuswindow address:") + address.toUtf8())) {
    return true;
  }
  phase_ = Phase::Idle;
  return false;
}

void HyprlandBackend::runLuaWindowActivation() {
  phase_ = Phase::LuaWindowActivation;
  const QByteArray selector = QByteArrayLiteral("address:") + escapeLuaString(window_activation_address_);
  const QByteArray command =
      QByteArrayLiteral("dispatch hl.dsp.focus({ window = \"") + selector + QByteArrayLiteral("\" })");
  if (!transport_->runCommand(command)) {
    fail(QStringLiteral("Hyprland Lua window activation failed"));
  }
}

void HyprlandBackend::runLuaActivation() {
  phase_ = Phase::LuaActivation;
  QByteArray command;
  QString diagnostic;
  if (activation_is_special_) {
    const QString name = activation_id_.sliced(QStringLiteral("special:").size());
    command = QByteArrayLiteral("dispatch hl.dsp.workspace.toggle_special(\"") + escapeLuaString(name) +
              QByteArrayLiteral("\")");
    diagnostic = QStringLiteral("Hyprland Lua special workspace activation failed");
  } else {
    command =
        QByteArrayLiteral("dispatch hl.dsp.focus({ workspace = ") + activation_id_.toUtf8() + QByteArrayLiteral(" })");
    diagnostic = QStringLiteral("Hyprland Lua workspace activation failed");
  }
  if (!transport_->runCommand(command)) {
    fail(diagnostic);
  }
}

void HyprlandBackend::publishClients(const QByteArray& clients_json) {
  const QJsonDocument monitors = QJsonDocument::fromJson(monitors_);
  const QJsonDocument workspaces = QJsonDocument::fromJson(workspaces_);
  const auto clients = parseHyprlandClientsJson(clients_json);
  if (!monitors.isArray() || !workspaces.isArray() || !clients) {
    fail(QStringLiteral("invalid Hyprland snapshot response"));
    return;
  }
  CompositorSnapshot snapshot{
      .connected = true,
      .capabilities =
          {
              .workspace_listing = true,
              .workspace_activation = true,
              .active_window = true,
              .focused_output = true,
              .urgency = true,
              .occupancy = true,
          },
  };
  QHash<int, QString> workspace_outputs;
  QSet<int> active_workspaces;
  collectMonitors(monitors.array(), &snapshot, &workspace_outputs, &active_workspaces);
  QHash<int, int> window_counts;
  QHash<int, HyprlandClientInfo> focused_clients;
  collectClients(*clients, &window_counts, &focused_clients);
  numbered_ = {.eligible = true};
  special_workspaces_.clear();
  appendWorkspaces(workspaces.array(), *clients, workspace_outputs, active_workspaces, window_counts, focused_clients,
                   &urgent_addresses_, &snapshot, &numbered_, &special_workspaces_);
  emit specialWorkspacesChanged();
  QList<WindowActivationCandidate> activation_candidates;
  QList<QString> activation_addresses;
  for (const HyprlandClientInfo& client : *clients) {
    if (client.pid == 0 || client.address.isEmpty()) {
      continue;
    }
    snapshot.windows.append(
        {.id = client.address, .title = client.title, .app_id = client.app_class, .pid = client.pid});
    activation_candidates.append({.pid = client.pid, .title = client.title});
    activation_addresses.append(client.address);
  }
  activation_candidates_ = std::move(activation_candidates);
  activation_addresses_ = std::move(activation_addresses);
  snapshot.capabilities.window_activation = true;
  emit snapshotReady(std::move(snapshot));
}

WindowActivationResult HyprlandBackend::requestWindowActivation(const WindowActivationRequest& request) {
  const WindowActivationResolution resolution = resolveWindowActivation(request, activation_candidates_);
  if (resolution.result != WindowActivationResult::Accepted) {
    return resolution.result;
  }
  const QString address = activation_addresses_.at(*resolution.candidate_index);
  if (phase_ != Phase::Idle || transport_->hasRunningCommand()) {
    if (pending_window_address_) {
      return WindowActivationResult::Busy;
    }
    pending_window_address_ = address;
    return WindowActivationResult::Accepted;
  }
  if (!beginWindowActivation(address)) {
    emit snapshotReady({.diagnostic = QStringLiteral("Hyprland window activation transport failed")});
    scheduleRefresh();
    return WindowActivationResult::Failed;
  }
  return WindowActivationResult::Accepted;
}

void HyprlandBackend::activateWorkspace(const QString& workspace_id) {
  if (numbered_.assignments.contains(workspace_id)) {
    dispatchWorkspace(workspace_id);
  }
}
void HyprlandBackend::activateNumberedSlot(int slot) {
  if (slot > 0) {
    dispatchWorkspace(QString::number(slot));
  }
}
void HyprlandBackend::activateSpecialWorkspace(const QString& identifier) {
  for (const auto& value : special_workspaces_) {
    if (value.toMap().value(QStringLiteral("id")).toString() == identifier) {
      dispatchWorkspace(identifier);
      return;
    }
  }
}
void HyprlandBackend::dispatchWorkspace(const QString& workspace_id) {
  const bool special = workspace_id.startsWith(QStringLiteral("special:"));
  bool valid = false;
  workspace_id.toInt(&valid);
  if (!valid && !special) {
    return;
  }
  if (phase_ != Phase::Idle || transport_->hasRunningCommand()) {
    pending_activation_ = workspace_id;
    return;
  }
  activation_id_ = workspace_id;
  activation_is_special_ = special;
  phase_ = Phase::WorkspaceActivation;
  const QByteArray command = special ? QByteArrayLiteral("dispatch togglespecialworkspace ") +
                                           workspace_id.sliced(QStringLiteral("special:").size()).toUtf8()
                                     : QByteArrayLiteral("dispatch workspace ") + workspace_id.toUtf8();
  if (!transport_->runCommand(command)) {
    phase_ = Phase::Idle;
    fail(QStringLiteral("Hyprland workspace activation failed"));
  }
}

void HyprlandBackend::fail(const QString& diagnostic) {
  phase_ = Phase::Idle;
  emit snapshotReady({.diagnostic = diagnostic});
}

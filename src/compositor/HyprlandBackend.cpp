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

class WorkspaceIndex {
 public:
  explicit WorkspaceIndex(const QJsonArray& workspaces) {
    for (const auto value : workspaces) {
      const auto reference = parseHyprlandWorkspaceReference(value.toObject());
      const QString identity = reference.identity();
      if (identity.isEmpty() || reference.name.isEmpty() ||
          (!reference.isSpecial() && (!reference.id || *reference.id == 0))) {
        continue;
      }
      identities_.insert(identity);
      if (reference.id && *reference.id != 0) {
        ids_.insert(*reference.id, identity);
      }
      if (reference.isSpecial()) {
        names_.insert(reference.name, identity);
      }
    }
  }
  [[nodiscard]] QString resolve(const HyprlandWorkspaceReference& reference) const {
    if (!reference.address.isEmpty()) {
      const QString identity = reference.identity();
      return identities_.contains(identity) ? identity : QString{};
    }
    if (reference.isSpecial() && names_.contains(reference.name)) {
      return names_.value(reference.name);
    }
    return reference.id ? ids_.value(*reference.id) : QString{};
  }

 private:
  QSet<QString> identities_;
  QHash<int, QString> ids_;
  QHash<QString, QString> names_;
};

void collectMonitors(const QJsonArray& monitors, const WorkspaceIndex& index, QSet<QString>* special_outputs,
                     CompositorSnapshot* snapshot, QHash<QString, QString>* outputs, QSet<QString>* active_workspaces) {
  for (const auto value : monitors) {
    const QJsonObject monitor = value.toObject();
    const QString output = monitor.value(QStringLiteral("name")).toString();
    const auto collect_active = [&output, &index, outputs, active_workspaces](const QJsonObject& workspace) {
      const auto reference = parseHyprlandWorkspaceReference(workspace);
      const QString workspace_id = index.resolve(reference);
      if (!output.isEmpty() && !workspace_id.isEmpty()) {
        outputs->insert(workspace_id, output);
        active_workspaces->insert(workspace_id);
      }
    };
    collect_active(monitor.value(QStringLiteral("activeWorkspace")).toObject());
    const auto special = monitor.value(QStringLiteral("specialWorkspace")).toObject();
    collect_active(special);
    if (!index.resolve(parseHyprlandWorkspaceReference(special)).isEmpty()) {
      special_outputs->insert(output);
    }
    if (monitor.value(QStringLiteral("focused")).toBool(false)) {
      snapshot->focused_output = output;
    }
  }
}

void collectClients(const QList<HyprlandClientInfo>& clients, const WorkspaceIndex& index,
                    QHash<QString, int>* window_counts, QHash<QString, HyprlandClientInfo>* focused_clients) {
  for (const HyprlandClientInfo& client : clients) {
    if (client.app_class.isEmpty() || client.title.isEmpty()) {
      continue;
    }
    const QString identity = index.resolve(client.workspace);
    if (identity.isEmpty()) {
      continue;
    }
    ++(*window_counts)[identity];
    if (!focused_clients->contains(identity) ||
        client.focus_history_id < focused_clients->value(identity).focus_history_id) {
      focused_clients->insert(identity, client);
    }
  }
}

void clearActiveWorkspaceUrgency(const QList<HyprlandClientInfo>& clients, const QString& workspace_id,
                                 const WorkspaceIndex& index, QSet<QString>* urgent_addresses) {
  urgent_addresses->removeIf([&clients, workspace_id, &index](const QString& address) {
    return std::ranges::any_of(clients, [&address, workspace_id, &index](const HyprlandClientInfo& client) {
      return index.resolve(client.workspace) == workspace_id && client.address.endsWith(address, Qt::CaseInsensitive);
    });
  });
}

[[nodiscard]] bool validWorkspace(const HyprlandWorkspaceReference& reference, const QString& workspace_id,
                                  const QString& name) {
  return !workspace_id.isEmpty() && !name.isEmpty() && (reference.isSpecial() || (reference.id && *reference.id != 0));
}

void appendWorkspaces(const QJsonArray& workspaces, const WorkspaceIndex& index, const QSet<QString>& special_outputs,
                      const QList<HyprlandClientInfo>& clients, const QHash<QString, QString>& workspace_outputs,
                      const QSet<QString>& active_workspaces, const QHash<QString, int>& window_counts,
                      const QHash<QString, HyprlandClientInfo>& focused_clients, QSet<QString>* urgent_addresses,
                      CompositorSnapshot* snapshot, NumberedWorkspaceState* numbered, QVariantList* specials) {
  int order = 0;
  for (const auto value : workspaces) {
    const QJsonObject workspace = value.toObject();
    const auto reference = parseHyprlandWorkspaceReference(workspace);
    const QString workspace_id = index.resolve(reference);
    const QString name = workspace.value(QStringLiteral("name")).toString();
    if (!validWorkspace(reference, workspace_id, name)) {
      continue;
    }
    const bool special = reference.isSpecial();
    const bool active = active_workspaces.contains(workspace_id);
    const QString output = workspace_outputs.value(workspace_id, workspace.value(QStringLiteral("monitor")).toString());
    const bool urgent = !special && std::ranges::any_of(clients, [urgent_addresses, workspace_id,
                                                                  &index](const HyprlandClientInfo& client) {
      return index.resolve(client.workspace) == workspace_id && addressMatches(*urgent_addresses, client.address);
    });
    if (active) {
      clearActiveWorkspaceUrgency(clients, workspace_id, index, urgent_addresses);
    }
    CompositorWorkspace entry{
        .id = special ? name : QString::number(*reference.id),
        .display_name = name,
        .stable_order = order++,
        .outputs = output.isEmpty() ? QStringList{} : QStringList{output},
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
      snapshot->special_workspaces.append(entry);
      specials->append(QVariantMap{
          {QStringLiteral("id"), entry.id},
          {QStringLiteral("name"), entry.display_name},
          {QStringLiteral("active"), entry.active},
          {QStringLiteral("urgent"), entry.urgent},
          {QStringLiteral("occupied"), entry.occupied.value_or(false)},
          {QStringLiteral("monitorNames"), entry.outputs},
      });
    } else {
      if (*reference.id > 0) {
        numbered->assignments.insert(entry.id, *reference.id);
      }
      if (*reference.id <= 0 || name != QString::number(*reference.id)) {
        numbered->eligible = false;
      }
      snapshot->workspaces.append(std::move(entry));
    }
    if (active && focused_clients.contains(workspace_id) &&
        (special || !special_outputs.contains(workspace_outputs.value(workspace_id)))) {
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
      window_ids_.clear();
      workspace_selectors_.clear();
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
  if (refresh_scheduled_) {
    return;
  }
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
    const auto target = std::exchange(pending_window_address_, std::nullopt).value();
    if ((!target.revalidate || window_ids_.contains(target.id)) && !beginWindowActivation(target.id)) {
      emit snapshotReady({.diagnostic = QStringLiteral("Hyprland window activation transport failed")});
      refresh_dirty_ = true;
      scheduleRefresh();
    }
    if (phase_ != Phase::Idle) {
      return;
    }
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
    const QString name = activation_id_.mid(QStringLiteral("special:").size());
    command = QByteArrayLiteral("dispatch hl.dsp.workspace.toggle_special(\"") + escapeLuaString(name) +
              QByteArrayLiteral("\")");
    diagnostic = QStringLiteral("Hyprland Lua special workspace activation failed");
  } else {
    const QByteArray selector =
        activation_id_.startsWith(QStringLiteral("name:"))
            ? QByteArrayLiteral("\"") + escapeLuaString(activation_id_) + QByteArrayLiteral("\"")
            : activation_id_.toUtf8();
    command = QByteArrayLiteral("dispatch hl.dsp.focus({ workspace = ") + selector + QByteArrayLiteral(" })");
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
              .window_listing = true,
              .workspace_listing = true,
              .workspace_activation = true,
              .active_window = true,
              .focused_output = true,
              .urgency = true,
              .occupancy = true,
              .window_workspace_membership = true,
          },
  };
  const WorkspaceIndex index(workspaces.array());
  QSet<QString> special_outputs;
  QHash<QString, QString> workspace_outputs;
  QSet<QString> active_workspaces;
  collectMonitors(monitors.array(), index, &special_outputs, &snapshot, &workspace_outputs, &active_workspaces);
  QHash<QString, int> window_counts;
  QHash<QString, HyprlandClientInfo> focused_clients;
  collectClients(*clients, index, &window_counts, &focused_clients);
  numbered_ = {.eligible = true};
  special_workspaces_.clear();
  appendWorkspaces(workspaces.array(), index, special_outputs, *clients, workspace_outputs, active_workspaces,
                   window_counts, focused_clients, &urgent_addresses_, &snapshot, &numbered_, &special_workspaces_);
  emit specialWorkspacesChanged();
  QList<WindowActivationCandidate> activation_candidates;
  QList<QString> activation_addresses;
  window_ids_.clear();
  QHash<QString, QString> workspace_ids;
  workspace_selectors_.clear();
  for (const auto value : workspaces.array()) {
    const auto reference = parseHyprlandWorkspaceReference(value.toObject());
    const QString identity = index.resolve(reference);
    if (!identity.isEmpty()) {
      const QString id = reference.isSpecial() ? reference.name : QString::number(reference.id.value_or(0));
      workspace_ids.insert(identity, id);
      if (!reference.isSpecial()) {
        workspace_selectors_.insert(id, *reference.id > 0 ? id : QStringLiteral("name:") + reference.name);
      }
      if (!workspace_outputs.contains(identity)) {
        workspace_outputs.insert(identity, value.toObject().value(QStringLiteral("monitor")).toString());
      }
    }
  }
  for (const HyprlandClientInfo& client : *clients) {
    if (client.address.isEmpty()) {
      continue;
    }
    const QString identity = index.resolve(client.workspace);
    const QString workspace_id = workspace_ids.value(identity);
    const QString output = workspace_outputs.value(identity);
    window_ids_.insert(client.address);
    snapshot.windows.append({
        .id = client.address,
        .title = client.title,
        .app_id = client.app_class,
        .outputs = output.isEmpty() ? QStringList{} : QStringList{output},
        .activated =
            output == snapshot.focused_output && active_workspaces.contains(identity) && client.focus_history_id == 0,
        .operations = {WindowCommand::Activate},
        .pid = client.pid,
        .workspace_id = workspace_id.isEmpty() ? std::nullopt : std::optional(workspace_id),
        .layout_rect = client.layout_rect,
    });
    if (client.pid != 0) {
      activation_candidates.append({.pid = client.pid, .title = client.title});
      activation_addresses.append(client.address);
    }
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
  switch (activateWindow(address, false)) {
    case WindowCommandResult::Accepted:
      return WindowActivationResult::Accepted;
    case WindowCommandResult::Busy:
      return WindowActivationResult::Busy;
    default:
      return WindowActivationResult::Failed;
  }
}

WindowCommandResult HyprlandBackend::requestWindowCommand(const QString& identifier, WindowCommand command) {
  if (command != WindowCommand::Activate) {
    return WindowCommandResult::Unsupported;
  }
  if (!window_ids_.contains(identifier)) {
    return WindowCommandResult::InvalidWindow;
  }
  return activateWindow(identifier, true);
}

WindowCommandResult HyprlandBackend::activateWindow(const QString& address, bool revalidate) {
  if (phase_ != Phase::Idle || transport_->hasRunningCommand()) {
    if (pending_window_address_) {
      return WindowCommandResult::Busy;
    }
    pending_window_address_ = PendingWindow{.id = address, .revalidate = revalidate};
    return WindowCommandResult::Accepted;
  }
  if (!beginWindowActivation(address)) {
    emit snapshotReady({.diagnostic = QStringLiteral("Hyprland window activation transport failed")});
    scheduleRefresh();
    return WindowCommandResult::Failed;
  }
  return WindowCommandResult::Accepted;
}

void HyprlandBackend::activateWorkspace(const QString& workspace_id) {
  if (workspace_selectors_.contains(workspace_id)) {
    dispatchWorkspace(workspace_selectors_.value(workspace_id));
  } else {
    activateSpecialWorkspace(workspace_id);
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
  const bool special = workspace_id == QStringLiteral("special") || workspace_id.startsWith(QStringLiteral("special:"));
  bool valid = false;
  workspace_id.toInt(&valid);
  if (!valid && !special && !workspace_id.startsWith(QStringLiteral("name:"))) {
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
                                           workspace_id.mid(QStringLiteral("special:").size()).toUtf8()
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

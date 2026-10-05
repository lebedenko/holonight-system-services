#include "LabwcBackend.h"

#include "ForeignToplevelState.h"

#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <QtGui/qguiapplication_platform.h>

#include <algorithm>
#include <cstring>
#include <span>

class LabwcWindow final : public QtWayland::zwlr_foreign_toplevel_handle_v1 {
 public:
  [[nodiscard]] QList<WindowCommand> operations() const {
    return foreignToplevelOperations(static_cast<int>(version()));
  }
  LabwcWindow(struct ::zwlr_foreign_toplevel_handle_v1* handle, LabwcBackend* backend)
      : QtWayland::zwlr_foreign_toplevel_handle_v1(handle), backend_(backend) {}
  LabwcWindow(const LabwcWindow&) = delete;
  LabwcWindow& operator=(const LabwcWindow&) = delete;
  LabwcWindow(LabwcWindow&&) = delete;
  LabwcWindow& operator=(LabwcWindow&&) = delete;
  ~LabwcWindow() override {
    if (isInitialized()) {
      destroy();
    }
  }

 protected:
  void zwlr_foreign_toplevel_handle_v1_title(const QString& title) override { state_.pending.window.title = title; }
  void zwlr_foreign_toplevel_handle_v1_app_id(const QString& identifier) override {
    state_.pending.window.app_id = identifier;
  }
  void zwlr_foreign_toplevel_handle_v1_output_enter(wl_output* output) override {
    state_.pending.outputs.insert(output);
  }
  void zwlr_foreign_toplevel_handle_v1_output_leave(wl_output* output) override {
    state_.pending.outputs.remove(output);
  }
  void zwlr_foreign_toplevel_handle_v1_state(wl_array* states) override {
    bool activated = false;
    state_.pending.minimized = false;
    state_.pending.maximized = false;
    state_.pending.fullscreen = false;
    const std::span bytes(static_cast<const char*>(states->data), states->size);
    for (size_t i = 0; i + sizeof(uint32_t) <= states->size; i += sizeof(uint32_t)) {
      uint32_t protocol_state{};
      std::memcpy(&protocol_state, bytes.subspan(i, sizeof(protocol_state)).data(), sizeof(protocol_state));
      activated |= protocol_state == state_activated;
      state_.pending.minimized |= protocol_state == state_minimized;
      state_.pending.maximized |= protocol_state == state_maximized;
      state_.pending.fullscreen |= protocol_state == state_fullscreen;
    }
    state_.setActivated(activated, backend_->activation_order_);
  }
  // Parent relationships do not establish authoritative workspace membership or geometry.
  void zwlr_foreign_toplevel_handle_v1_parent(
      [[maybe_unused]] struct ::zwlr_foreign_toplevel_handle_v1* parent) override {}
  void zwlr_foreign_toplevel_handle_v1_done() override {
    state_.commit();
    backend_->schedulePublish();
  }
  void zwlr_foreign_toplevel_handle_v1_closed() override {
    backend_->windows_.removeOne(this);
    backend_->schedulePublish();
    delete this;
  }

 private:
  friend class LabwcBackend;
  ForeignToplevelState state_;
  QString identifier_{QUuid::createUuid().toString(QUuid::WithoutBraces)};
  LabwcBackend* backend_;
};

class LabwcProtocol final : public QWaylandClientExtensionTemplate<LabwcProtocol>,
                            public QtWayland::zwlr_foreign_toplevel_manager_v1 {
 public:
  explicit LabwcProtocol(LabwcBackend* backend)
      : QWaylandClientExtensionTemplate(backend->maximum_protocol_version_), backend_(backend) {}
  void bind() { initialize(); }
  LabwcProtocol(const LabwcProtocol&) = delete;
  LabwcProtocol& operator=(const LabwcProtocol&) = delete;
  LabwcProtocol(LabwcProtocol&&) = delete;
  LabwcProtocol& operator=(LabwcProtocol&&) = delete;
  ~LabwcProtocol() override {
    if (isInitialized()) {
      if (isActive() && !finished_) {
        stop();
      }
      ::zwlr_foreign_toplevel_manager_v1_destroy(object());
    }
  }

 protected:
  void zwlr_foreign_toplevel_manager_v1_toplevel(struct ::zwlr_foreign_toplevel_handle_v1* handle) override {
    backend_->windows_.append(new LabwcWindow(handle, backend_));
  }
  void zwlr_foreign_toplevel_manager_v1_finished() override {
    finished_ = true;
    backend_->protocolFinished();
  }

 private:
  LabwcBackend* backend_;
  bool finished_{false};
};

LabwcBackend::LabwcBackend(QObject* parent, int maximum_protocol_version)
    : CompositorBackend(parent), maximum_protocol_version_(std::clamp(maximum_protocol_version, 1, 3)) {
  connect(&workspace_, &CompositorBackend::snapshotReady, this, [this](CompositorSnapshot snapshot) {
    workspace_snapshot_ = std::move(snapshot);
    schedulePublish();
  });
  reconnect_timer_.setSingleShot(true);
  reconnect_timer_.setInterval(1000);
  connect(&reconnect_timer_, &QTimer::timeout, this, &LabwcBackend::connectProtocol);
  connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen*) { schedulePublish(); });
  connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) { schedulePublish(); });
}
LabwcBackend::~LabwcBackend() { qDeleteAll(windows_); }
void LabwcBackend::start() {
  workspace_.start();
  if (!protocol_) {
    connectProtocol();
  }
}
void LabwcBackend::activateWorkspace(const QString& identifier) { workspace_.activateWorkspace(identifier); }
void LabwcBackend::connectProtocol() {
  qDeleteAll(windows_);
  windows_.clear();
  protocol_.reset();
  available_ = false;
  protocol_ = std::make_unique<LabwcProtocol>(this);
  connect(protocol_.get(), &QWaylandClientExtension::activeChanged, this, [this] {
    if (!protocol_->isActive()) {
      protocolFinished();
      return;
    }
    available_ = true;
    reconnect_timer_.stop();
    schedulePublish();
  });
  protocol_->bind();
  if (protocol_->isActive()) {
    available_ = true;
    schedulePublish();
  } else {
    {
      protocolFinished();
    }
  }
}
void LabwcBackend::protocolFinished() {
  available_ = false;
  qDeleteAll(windows_);
  windows_.clear();
  schedulePublish();
  reconnect_timer_.start();
}
void LabwcBackend::schedulePublish() {
  if (publish_pending_) {
    return;
  }
  publish_pending_ = true;
  QTimer::singleShot(0, this, [this] {
    publish_pending_ = false;
    publish();
  });
}
WindowCommandResult LabwcBackend::requestWindowCommand(const QString& identifier, WindowCommand command) {
  if (!available_) {
    return WindowCommandResult::Disconnected;
  }
  LabwcWindow* target = nullptr;
  for (auto* window : std::as_const(windows_)) {
    if (window->identifier_ == identifier && window->state_.ready) {
      target = window;
    }
  }
  if (target == nullptr) {
    return WindowCommandResult::InvalidWindow;
  }
  if (!target->operations().contains(command)) {
    return WindowCommandResult::Unsupported;
  }
  switch (command) {
    case WindowCommand::Activate: {
      auto* native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
      auto* seat = (native != nullptr) ? native->seat() : nullptr;
      if (seat == nullptr) {
        return WindowCommandResult::MissingSeat;
      }
      target->activate(seat);
      break;
    }
    case WindowCommand::Minimize:
      target->set_minimized();
      break;
    case WindowCommand::Restore:
      target->unset_minimized();
      break;
    case WindowCommand::Maximize:
      target->set_maximized();
      break;
    case WindowCommand::Unmaximize:
      target->unset_maximized();
      break;
    case WindowCommand::Fullscreen:
      target->set_fullscreen(nullptr);
      break;
    case WindowCommand::Unfullscreen:
      target->unset_fullscreen();
      break;
    case WindowCommand::Close:
      target->close();
      break;
  }
  return WindowCommandResult::Accepted;
}
void LabwcBackend::publish() {
  QHash<QString, CompositorActiveWindow> windows;
  QList<CompositorWindow> inventory;
  QHash<wl_output*, QString> names;
  for (auto* screen : QGuiApplication::screens()) {
    auto* native = screen->nativeInterface<QNativeInterface::QWaylandScreen>();
    if (native != nullptr) {
      names.insert(native->output(), screen->name());
    }
  }
  if (available_) {
    QList<const ForeignToplevelState*> states;
    for (const auto* window : std::as_const(windows_)) {
      if (!window->state_.ready) {
        continue;
      }
      states.append(&window->state_);
      const auto& value = window->state_.committed;
      auto outputs = window->state_.onOutputs(names).keys();
      outputs.sort();
      inventory.append({
          .id = window->identifier_,
          .title = value.window.title,
          .app_id = value.window.app_id,
          .outputs = outputs,
          .activated = value.activated,
          .minimized = value.minimized,
          .maximized = value.maximized,
          .fullscreen = value.fullscreen,
          .operations = window->operations(),
      });
    }
    if (const auto* active = ForeignToplevelState::active(states)) {
      windows = active->onOutputs(names);
    }
  }
  auto snapshot = mergeLabwcSnapshot(workspace_snapshot_, available_, windows);
  snapshot.capabilities.window_listing = available_;
  snapshot.windows = std::move(inventory);
  emit snapshotReady(snapshot);
}

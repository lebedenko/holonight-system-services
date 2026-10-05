#pragma once

#include "CompositorBackend.h"
#include "GenericWorkspaceState.h"
#include "qwayland-ext-workspace-v1.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>

#include <memory>

class GenericBackend;
class GenericProtocol;

class GenericWorkspaceHandle final : public QtWayland::ext_workspace_handle_v1 {
 public:
  GenericWorkspaceHandle(struct ::ext_workspace_handle_v1* handle, GenericBackend* backend, int order);
  ~GenericWorkspaceHandle() override;
  GenericWorkspaceHandle(const GenericWorkspaceHandle&) = delete;
  GenericWorkspaceHandle& operator=(const GenericWorkspaceHandle&) = delete;
  GenericWorkspaceHandle(GenericWorkspaceHandle&&) = delete;
  GenericWorkspaceHandle& operator=(GenericWorkspaceHandle&&) = delete;

 protected:
  void ext_workspace_handle_v1_id(const QString& identifier) override;
  void ext_workspace_handle_v1_capabilities(uint32_t capabilities) override;
  void ext_workspace_handle_v1_name(const QString& name) override;
  void ext_workspace_handle_v1_state(uint32_t state) override;
  void ext_workspace_handle_v1_removed() override;

 private:
  friend class GenericBackend;
  struct ::ext_workspace_handle_v1* raw_;
  GenericBackend* backend_;
  GenericWorkspaceState state_;
};

class GenericWorkspaceGroup final : public QtWayland::ext_workspace_group_handle_v1 {
 public:
  GenericWorkspaceGroup(struct ::ext_workspace_group_handle_v1* group, GenericBackend* backend);
  ~GenericWorkspaceGroup() override;
  GenericWorkspaceGroup(const GenericWorkspaceGroup&) = delete;
  GenericWorkspaceGroup& operator=(const GenericWorkspaceGroup&) = delete;
  GenericWorkspaceGroup(GenericWorkspaceGroup&&) = delete;
  GenericWorkspaceGroup& operator=(GenericWorkspaceGroup&&) = delete;
  [[nodiscard]] QStringList outputNames() const;

 protected:
  void ext_workspace_group_handle_v1_output_enter(struct ::wl_output* output) override;
  void ext_workspace_group_handle_v1_output_leave(struct ::wl_output* output) override;
  void ext_workspace_group_handle_v1_workspace_enter(struct ::ext_workspace_handle_v1* workspace) override;
  void ext_workspace_group_handle_v1_workspace_leave(struct ::ext_workspace_handle_v1* workspace) override;
  void ext_workspace_group_handle_v1_removed() override;

 private:
  friend class GenericBackend;
  GenericBackend* backend_;
  QString id_;
  QList<struct ::wl_output*> outputs_;
  QSet<struct ::ext_workspace_handle_v1*> workspaces_;
};

class GenericProtocol final : public QWaylandClientExtensionTemplate<GenericProtocol>,
                              public QtWayland::ext_workspace_manager_v1 {
 public:
  explicit GenericProtocol(GenericBackend* backend);
  GenericProtocol(const GenericProtocol&) = delete;
  GenericProtocol& operator=(const GenericProtocol&) = delete;
  GenericProtocol(GenericProtocol&&) = delete;
  GenericProtocol& operator=(GenericProtocol&&) = delete;
  ~GenericProtocol() override;

 protected:
  void ext_workspace_manager_v1_workspace_group(struct ::ext_workspace_group_handle_v1* group) override;
  void ext_workspace_manager_v1_workspace(struct ::ext_workspace_handle_v1* workspace) override;
  void ext_workspace_manager_v1_done() override;
  void ext_workspace_manager_v1_finished() override;

 private:
  GenericBackend* backend_;
};

class GenericBackend final : public CompositorBackend {
  Q_OBJECT

 public:
  explicit GenericBackend(QObject* parent = nullptr);
  ~GenericBackend() override;
  GenericBackend(const GenericBackend&) = delete;
  GenericBackend& operator=(const GenericBackend&) = delete;
  GenericBackend(GenericBackend&&) = delete;
  GenericBackend& operator=(GenericBackend&&) = delete;
  void start() override;
  void activateWorkspace(const QString& workspace_id) override;

 private:
  friend class GenericWorkspaceHandle;
  friend class GenericWorkspaceGroup;
  friend class GenericProtocol;
  void publishSnapshotOnDone();
  void remove(GenericWorkspaceHandle* handle);
  void remove(GenericWorkspaceGroup* group);
  QList<GenericWorkspaceGroup*> groups_;
  QHash<struct ::ext_workspace_handle_v1*, GenericWorkspaceHandle*> handles_;
  int next_order_{0};
  int next_group_{0};
  void connectProtocol();
  void clearState();
  void protocolFinished();
  std::unique_ptr<GenericProtocol> protocol_;
  QTimer reconnect_timer_;
  bool finished_{false};
};

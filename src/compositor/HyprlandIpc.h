#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

#include <optional>

struct HyprlandActiveWindow {
  QString app_class;
  QString title;
  QString category;

  [[nodiscard]] bool isEmpty() const { return title.isEmpty() && app_class.isEmpty(); }
};

struct HyprlandFocusedMonitor {
  QString monitor_name;
  QString workspace_name;
};

struct HyprlandMonitorInfo {
  QString name;
  int active_workspace_id{0};
};

struct HyprlandWorkspaceReference {
  QString address;
  QString name;
  QString type;
  std::optional<int> id;
  [[nodiscard]] bool isSpecial() const;
  [[nodiscard]] QString identity() const;
};
[[nodiscard]] HyprlandWorkspaceReference parseHyprlandWorkspaceReference(const QJsonObject& object);

struct HyprlandClientInfo {
  QString address;
  QString app_class;
  QString title;
  quint32 pid{0};
  int workspace_id{0};
  int focus_history_id{0};
  HyprlandWorkspaceReference workspace;
};

struct HyprlandOpenWindow {
  QString address;
  QString workspace_name;
  QString app_class;
  QString title;
};

struct HyprlandWorkspaceSnapshot {
  QSet<int> occupied_workspace_ids;
};

[[nodiscard]] std::optional<HyprlandActiveWindow> parseHyprlandActiveWindowEvent(const QByteArray& line);
[[nodiscard]] std::optional<HyprlandActiveWindow> parseHyprlandActiveWindowJson(const QByteArray& response);
[[nodiscard]] std::optional<int> parseHyprlandWorkspaceEvent(const QByteArray& line);
[[nodiscard]] std::optional<HyprlandFocusedMonitor> parseHyprlandFocusedMonitorEvent(const QByteArray& line);
[[nodiscard]] std::optional<QString> parseHyprlandUrgentWindowEvent(const QByteArray& line);
[[nodiscard]] std::optional<int> parseHyprlandActiveWorkspaceJson(const QByteArray& response);
[[nodiscard]] std::optional<HyprlandWorkspaceSnapshot> parseHyprlandWorkspacesJson(const QByteArray& response);
[[nodiscard]] bool isHyprlandWorkspaceRefreshEvent(const QByteArray& line);
[[nodiscard]] std::optional<HyprlandOpenWindow> parseHyprlandOpenWindowEvent(const QByteArray& line);
[[nodiscard]] std::optional<QHash<QString, int>> parseHyprlandMonitorsJson(const QByteArray& response);
[[nodiscard]] std::optional<QString> parseHyprlandFocusedMonitorNameJson(const QByteArray& response);
[[nodiscard]] std::optional<QList<HyprlandClientInfo>> parseHyprlandClientsJson(const QByteArray& response);
[[nodiscard]] std::optional<int> workspaceIdForHyprlandClientAddress(const QList<HyprlandClientInfo>& clients,
                                                                     const QString& address);
[[nodiscard]] std::optional<int> workspaceIdForHyprlandClientAddressJson(const QByteArray& response,
                                                                         const QString& address);

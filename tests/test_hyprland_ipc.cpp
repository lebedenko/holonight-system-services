#include "HyprlandIpc.h"

#include <QTest>

#include <gtest/gtest.h>

TEST(HyprlandIpc, ParsesActiveWindowEvent) {
  const std::optional<HyprlandActiveWindow> parsed =
      parseHyprlandActiveWindowEvent("activewindow>>kitty,src/editor.cpp");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->app_class, QStringLiteral("kitty"));
  EXPECT_EQ(parsed->title, QStringLiteral("src/editor.cpp"));
}

TEST(HyprlandIpc, IgnoresUnrelatedEvent) { EXPECT_FALSE(parseHyprlandActiveWindowEvent("workspace>>2").has_value()); }

TEST(HyprlandIpc, MalformedActiveWindowEventClearsValues) {
  const std::optional<HyprlandActiveWindow> parsed = parseHyprlandActiveWindowEvent("activewindow>>");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->app_class.isEmpty());
  EXPECT_TRUE(parsed->title.isEmpty());
}

TEST(HyprlandIpc, KeepsCommasInTitlePayload) {
  const std::optional<HyprlandActiveWindow> parsed =
      parseHyprlandActiveWindowEvent("activewindow>>firefox,Issue #1, review, and merge");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->app_class, QStringLiteral("firefox"));
  EXPECT_EQ(parsed->title, QStringLiteral("Issue #1, review, and merge"));
}

TEST(HyprlandIpc, ParsesActiveWindowJson) {
  const std::optional<HyprlandActiveWindow> parsed =
      parseHyprlandActiveWindowJson(R"({"class":"Code","title":"holonight-shell"})");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->app_class, QStringLiteral("Code"));
  EXPECT_EQ(parsed->title, QStringLiteral("holonight-shell"));
}

TEST(HyprlandIpc, IgnoresMalformedActiveWindowJson) {
  EXPECT_FALSE(parseHyprlandActiveWindowJson("not json").has_value());
}

TEST(HyprlandIpc, WarnsOnMalformedActiveWindowJson) {
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandActiveWindowJson: expected JSON object");
  EXPECT_FALSE(parseHyprlandActiveWindowJson("not json").has_value());
}

TEST(HyprlandIpc, ParsesWorkspaceEvent) {
  EXPECT_EQ(parseHyprlandWorkspaceEvent("workspace>>7"), 7);
  EXPECT_FALSE(parseHyprlandWorkspaceEvent("workspace>>special:magic").has_value());
  EXPECT_FALSE(parseHyprlandWorkspaceEvent("activewindow>>kitty,title").has_value());
}

TEST(HyprlandIpc, ParsesFocusedMonitorEvent) {
  const auto result = parseHyprlandFocusedMonitorEvent("focusedmon>>DP-1,8");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->monitor_name, QStringLiteral("DP-1"));
  EXPECT_EQ(result->workspace_name, QStringLiteral("8"));
  EXPECT_FALSE(parseHyprlandFocusedMonitorEvent("focusedmon>>DP-1").has_value());
}

TEST(HyprlandIpc, ParsesUrgentWindowEvent) {
  const auto result = parseHyprlandUrgentWindowEvent("urgent>>abc123");

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, QStringLiteral("abc123"));
  EXPECT_FALSE(parseHyprlandUrgentWindowEvent("activewindow>>kitty,title").has_value());
  EXPECT_FALSE(parseHyprlandUrgentWindowEvent("urgent>>").has_value());
}

TEST(HyprlandIpc, ParsesActiveWorkspaceJson) {
  EXPECT_EQ(parseHyprlandActiveWorkspaceJson(R"({"id":4,"name":"4"})"), 4);
  EXPECT_FALSE(parseHyprlandActiveWorkspaceJson(R"({"id":0})").has_value());
}

TEST(HyprlandIpc, WarnsOnMalformedActiveWorkspaceJson) {
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandActiveWorkspaceJson: expected JSON object");
  EXPECT_FALSE(parseHyprlandActiveWorkspaceJson("not json").has_value());
}

TEST(HyprlandIpc, ParsesWorkspaceOccupancySnapshot) {
  const std::optional<HyprlandWorkspaceSnapshot> snapshot =
      parseHyprlandWorkspacesJson(R"([{"id":1,"windows":0},{"id":2,"windows":3},{"id":7,"windows":1}])");

  ASSERT_TRUE(snapshot.has_value());
  EXPECT_FALSE(snapshot->occupied_workspace_ids.contains(1));
  EXPECT_TRUE(snapshot->occupied_workspace_ids.contains(2));
  EXPECT_TRUE(snapshot->occupied_workspace_ids.contains(7));
}

TEST(HyprlandIpc, WorkspaceOccupancySnapshotWarnsOnMalformedShape) {
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandWorkspacesJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandWorkspacesJson("not json").has_value());
}

TEST(HyprlandIpc, DetectsWorkspaceRefreshEvents) {
  EXPECT_TRUE(isHyprlandWorkspaceRefreshEvent("openwindow>>address,workspace,class,title"));
  EXPECT_TRUE(isHyprlandWorkspaceRefreshEvent("closewindow>>address"));
  EXPECT_TRUE(isHyprlandWorkspaceRefreshEvent("focusedmon>>DP-1,2"));
  EXPECT_FALSE(isHyprlandWorkspaceRefreshEvent("activewindow>>kitty,title"));
}

TEST(HyprlandIpc, ParsesOpenWindowEventWithCommasInTitle) {
  const auto parsed = parseHyprlandOpenWindowEvent("openwindow>>abc123,7,firefox,Issue #1, review, and merge");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->address, QStringLiteral("abc123"));
  EXPECT_EQ(parsed->workspace_name, QStringLiteral("7"));
  EXPECT_EQ(parsed->app_class, QStringLiteral("firefox"));
  EXPECT_EQ(parsed->title, QStringLiteral("Issue #1, review, and merge"));
}

TEST(HyprlandIpc, IgnoresMalformedAndUnrelatedOpenWindowEvents) {
  EXPECT_FALSE(parseHyprlandOpenWindowEvent("activewindow>>firefox,title").has_value());
  EXPECT_FALSE(parseHyprlandOpenWindowEvent("openwindow>>abc123").has_value());
  EXPECT_FALSE(parseHyprlandOpenWindowEvent("openwindow>>abc123,7").has_value());
  EXPECT_FALSE(parseHyprlandOpenWindowEvent("openwindow>>abc123,7,firefox").has_value());
}

TEST(HyprlandIpc, ParsesMonitorWorkspaceMapFromJson) {
  const auto parsed = parseHyprlandMonitorsJson(R"json([
    {"name": "DP-1", "activeWorkspace": {"id": 1}},
    {"name": "HDMI-A-1", "activeWorkspace": {"id": 5}},
    {"name": "", "activeWorkspace": {"id": 8}},
    {"name": "ignored", "activeWorkspace": {"id": 0}},
    "not an object"
  ])json");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->size(), 2);
  EXPECT_EQ(parsed->value(QStringLiteral("DP-1")), 1);
  EXPECT_EQ(parsed->value(QStringLiteral("HDMI-A-1")), 5);
}

TEST(HyprlandIpc, ParsesFocusedMonitorNameFromJson) {
  const auto parsed = parseHyprlandFocusedMonitorNameJson(R"json([
    {"name": "DP-1", "focused": false},
    {"name": "HDMI-A-1", "focused": true}
  ])json");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, QStringLiteral("HDMI-A-1"));
}

TEST(HyprlandIpc, FocusedMonitorNameReturnsEmptyWhenNoMonitorIsFocused) {
  const auto parsed = parseHyprlandFocusedMonitorNameJson(R"json([
    {"name": "DP-1", "focused": false},
    {"name": "HDMI-A-1"}
  ])json");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_TRUE(parsed->isEmpty());
}

TEST(HyprlandIpc, RejectsInvalidMonitorJsonShapes) {
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandMonitorsJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandMonitorsJson("not json").has_value());
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandMonitorsJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandMonitorsJson(R"({"name":"DP-1"})").has_value());
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandFocusedMonitorNameJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandFocusedMonitorNameJson("not json").has_value());
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandFocusedMonitorNameJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandFocusedMonitorNameJson(R"({"name":"DP-1"})").has_value());
}

TEST(HyprlandIpc, ParsesClientsJsonAndFiltersEmptyIdentity) {
  const auto parsed = parseHyprlandClientsJson(R"json([
    {"address": "0xabc123", "class": "Code", "title": "holonight-shell", "workspace": {"id": 2}, "focusHistoryID": 0},
    {"class": "kitty", "title": "build", "workspace": {"id": 3}},
    {"class": "", "title": "missing class", "workspace": {"id": 4}},
    {"class": "firefox", "title": "", "workspace": {"id": 5}},
    42
  ])json");

  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->size(), 2);
  EXPECT_EQ(parsed->at(0).app_class, QStringLiteral("Code"));
  EXPECT_EQ(parsed->at(0).address, QStringLiteral("0xabc123"));
  EXPECT_EQ(parsed->at(0).title, QStringLiteral("holonight-shell"));
  EXPECT_EQ(parsed->at(0).pid, 0U);
  EXPECT_EQ(parsed->at(0).workspace_id, 2);
  EXPECT_EQ(parsed->at(0).focus_history_id, 0);
  EXPECT_EQ(parsed->at(1).app_class, QStringLiteral("kitty"));
  EXPECT_EQ(parsed->at(1).title, QStringLiteral("build"));
  EXPECT_EQ(parsed->at(1).workspace_id, 3);
  EXPECT_EQ(parsed->at(1).focus_history_id, std::numeric_limits<int>::max());
}

TEST(HyprlandIpc, ParsesPositiveClientPidWithExactActivationFields) {
  const auto parsed = parseHyprlandClientsJson(R"json([
    {"address":"0xabc","class":"kitty","title":"Exact title","pid":4294967295,"workspace":{"id":2}},
    {"address":"0xempty","class":"kitty","title":"","pid":42,"workspace":{"id":3}}
  ])json");

  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->size(), 2);
  EXPECT_EQ(parsed->at(0).pid, std::numeric_limits<quint32>::max());
  EXPECT_EQ(parsed->at(0).title, QStringLiteral("Exact title"));
  EXPECT_EQ(parsed->at(0).address, QStringLiteral("0xabc"));
  EXPECT_EQ(parsed->at(1).pid, 42U);
  EXPECT_TRUE(parsed->at(1).title.isEmpty());
}

TEST(HyprlandIpc, InvalidClientPidsAreNotActivationEligible) {
  const auto parsed = parseHyprlandClientsJson(R"json([
    {"address":"zero","class":"app","title":"zero","pid":0},
    {"address":"negative","class":"app","title":"negative","pid":-1},
    {"address":"fraction","class":"app","title":"fraction","pid":1.5},
    {"address":"missing","class":"app","title":"missing"},
    {"address":"large","class":"app","title":"large","pid":4294967296}
  ])json");

  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->size(), 5);
  for (const HyprlandClientInfo& client : *parsed) {
    EXPECT_EQ(client.pid, 0U);
  }
}

TEST(HyprlandIpc, RejectsInvalidClientsJsonShapes) {
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandClientsJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandClientsJson("not json").has_value());
  QTest::ignoreMessage(QtWarningMsg, "parseHyprlandClientsJson: expected JSON array");
  EXPECT_FALSE(parseHyprlandClientsJson(R"({"class":"Code"})").has_value());
}

TEST(HyprlandIpc, FindsWorkspaceForClientAddress) {
  const QList<HyprlandClientInfo> clients = {
      HyprlandClientInfo{.address = QStringLiteral("0xabc123"), .workspace_id = 7},
      HyprlandClientInfo{.address = QStringLiteral("0xdef456"), .workspace_id = 2},
  };

  EXPECT_EQ(workspaceIdForHyprlandClientAddress(clients, QStringLiteral("abc123")), 7);
  EXPECT_EQ(workspaceIdForHyprlandClientAddress(clients, QStringLiteral("c123")), 7);
  EXPECT_EQ(workspaceIdForHyprlandClientAddress(clients, QStringLiteral("0xDEF456")), 2);
  EXPECT_FALSE(workspaceIdForHyprlandClientAddress(clients, QStringLiteral("missing")).has_value());
}

TEST(HyprlandIpc, FindsWorkspaceForClientAddressJsonWithoutClassOrTitle) {
  const QByteArray clients = QByteArrayLiteral(R"json([
    {"address": "0xabc123", "class": "", "title": "", "workspace": {"id": 7}},
    {"address": "0xdef456", "workspace": {"id": 2}}
  ])json");

  EXPECT_EQ(workspaceIdForHyprlandClientAddressJson(clients, QStringLiteral("abc123")), 7);
  EXPECT_EQ(workspaceIdForHyprlandClientAddressJson(clients, QStringLiteral("c123")), 7);
  EXPECT_EQ(workspaceIdForHyprlandClientAddressJson(clients, QStringLiteral("0xDEF456")), 2);
  EXPECT_FALSE(workspaceIdForHyprlandClientAddressJson(clients, QStringLiteral("missing")).has_value());
}

TEST(HyprlandIpc, WorkspaceIdForClientAddressJsonWarnsOnMalformedShape) {
  QTest::ignoreMessage(QtWarningMsg, "workspaceIdForHyprlandClientAddressJson: expected JSON array");
  EXPECT_FALSE(
      workspaceIdForHyprlandClientAddressJson(QByteArrayLiteral("not json"), QStringLiteral("abc123")).has_value());
}

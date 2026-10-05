#include "SwayIpc.h"

#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

TEST(SwayIpc, DecodesPartialHeaderAndPayload) {
  const QByteArray encoded = encodeSwayIpcFrame(4, QByteArrayLiteral("{\"ok\":true}"));
  SwayIpcDecoder decoder;
  EXPECT_TRUE(decoder.append(encoded.first(5)));
  EXPECT_TRUE(decoder.takeFrames().isEmpty());
  EXPECT_TRUE(decoder.append(encoded.sliced(5, 10)));
  EXPECT_TRUE(decoder.takeFrames().isEmpty());
  EXPECT_TRUE(decoder.append(encoded.sliced(15)));
  const auto frames = decoder.takeFrames();
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames.front().type, 4U);
  EXPECT_EQ(frames.front().payload, QByteArrayLiteral("{\"ok\":true}"));
}

TEST(SwayIpc, RejectsInvalidMagicAndOversizedPayloadBeforeAllocation) {
  SwayIpcDecoder invalid;
  EXPECT_FALSE(invalid.append(QByteArrayLiteral("bad-ipc.......")));
  EXPECT_FALSE(invalid.error().isEmpty());

  QByteArray header = encodeSwayIpcFrame(1, {});
  const quint32 oversized_length = SwayIpcDecoder::kMaximumPayload + 1;
  QByteArray encoded_length;
  QDataStream stream(&encoded_length, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream << oversized_length;
  header.replace(6, encoded_length.size(), encoded_length);
  SwayIpcDecoder oversized;
  EXPECT_FALSE(oversized.append(header));
  EXPECT_TRUE(oversized.error().contains(QStringLiteral("8 MiB")));
}

TEST(SwayIpc, DecodesMultipleFramesWithoutLosingRemainder) {
  SwayIpcDecoder decoder;
  EXPECT_TRUE(decoder.append(encodeSwayIpcFrame(1, "a") + encodeSwayIpcFrame(2, "bc")));
  const auto frames = decoder.takeFrames();
  ASSERT_EQ(frames.size(), 2);
  EXPECT_EQ(frames.at(0).payload, QByteArrayLiteral("a"));
  EXPECT_EQ(frames.at(1).payload, QByteArrayLiteral("bc"));
}

TEST(SwayIpc, EscapesWorkspaceNamesForQuotedCommands) {
  EXPECT_EQ(escapeSwayWorkspaceName(QStringLiteral("dev \\\"quoted\\\" \\\\ path")),
            QStringLiteral("dev \\\\\\\"quoted\\\\\\\" \\\\\\\\ path"));
}

TEST(SwayIpc, ParsesNamedWorkspacesFloatingWindowsAndFocusedOutput) {
  const auto snapshot = parseSwaySnapshot(
      R"([{"num":-1,"name":"dev:web","visible":true,"focused":true,"urgent":true,"output":"DP-1"},{"num":2,"name":"2:chat","visible":false,"focused":false,"urgent":false,"output":"HDMI-A-1"}])",
      R"([{"name":"DP-1","focused":true},{"name":"HDMI-A-1","focused":false}])",
      R"({"type":"root","nodes":[{"type":"output","name":"DP-1","nodes":[{"type":"workspace","name":"dev:web","nodes":[],"floating_nodes":[{"type":"con","name":"Editor","app_id":"code","focused":true,"nodes":[],"floating_nodes":[]}]}]},{"type":"output","name":"__i3","nodes":[{"type":"workspace","name":"__i3_scratch","nodes":[{"type":"con","name":"ignored","focused":false,"nodes":[],"floating_nodes":[]}],"floating_nodes":[]}]}],"floating_nodes":[]})");
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->workspaces.size(), 2);
  EXPECT_EQ(snapshot->focused_output, QStringLiteral("DP-1"));
  EXPECT_EQ(snapshot->workspaces.at(0).id, QStringLiteral("name:dev:web"));
  EXPECT_TRUE(snapshot->workspaces.at(0).occupied.value());
  EXPECT_EQ(snapshot->active_windows.value(QStringLiteral("DP-1")).app_id, QStringLiteral("code"));
}

TEST(SwayIpc, InventoriesNestedTiledFloatingAndScratchWindowsWithExactTitles) {
  const auto refresh = parseSwayRefresh(
      R"([{"num":1,"name":"1","output":"DP-1"}])", R"([{"name":"DP-1","focused":true}])",
      R"({"type":"root","nodes":[{"type":"output","name":"DP-1","nodes":[{"type":"workspace","name":"1","nodes":[{"type":"con","id":101,"pid":42,"name":"Tiled","nodes":[{"type":"con","id":102,"pid":43,"name":"","nodes":[],"floating_nodes":[]}],"floating_nodes":[]}],"floating_nodes":[{"type":"con","id":103,"pid":44,"name":"Floating","nodes":[],"floating_nodes":[]}]}]},{"type":"output","name":"__i3","nodes":[{"type":"workspace","name":"__i3_scratch","nodes":[{"type":"con","id":104,"pid":45,"name":"Scratch","nodes":[],"floating_nodes":[]}],"floating_nodes":[]}]}],"floating_nodes":[]})");

  ASSERT_TRUE(refresh.has_value());
  ASSERT_EQ(refresh->windows.size(), 4);
  EXPECT_EQ(refresh->windows.at(0),
            (SwayWindowInfo{.candidate = {.pid = 42, .title = QStringLiteral("Tiled")}, .container_id = 101}));
  EXPECT_EQ(refresh->windows.at(1),
            (SwayWindowInfo{.candidate = {.pid = 43, .title = QString{}}, .container_id = 102}));
  EXPECT_EQ(refresh->windows.at(2).container_id, 103U);
  EXPECT_EQ(refresh->windows.at(3).container_id, 104U);
  EXPECT_TRUE(refresh->snapshot.workspaces.first().occupied.value_or(false));
  EXPECT_FALSE(refresh->snapshot.active_windows.contains(QStringLiteral("__i3")));
}

TEST(SwayIpc, RejectsInvalidPidAndContainerIdFromActivationInventory) {
  const auto refresh = parseSwayRefresh("[]", "[]",
                                        R"({"type":"root","nodes":[
        {"type":"con","id":1,"pid":1,"name":"valid"},
        {"type":"con","id":2,"pid":0},{"type":"con","id":3,"pid":-1},
        {"type":"con","id":4,"pid":1.5},{"type":"con","id":5},
        {"type":"con","id":6,"pid":4294967296},
        {"type":"con","id":0,"pid":2},{"type":"con","id":-1,"pid":3},
        {"type":"con","id":2.5,"pid":4},{"type":"con","pid":5},
        {"type":"con","id":18446744073709551616,"pid":6}
      ],"floating_nodes":[]})");

  ASSERT_TRUE(refresh.has_value());
  ASSERT_EQ(refresh->windows.size(), 1);
  EXPECT_EQ(refresh->windows.first().candidate.pid, 1U);
  EXPECT_EQ(refresh->windows.first().container_id, 1U);
}

TEST(SwayIpc, RejectsIncompleteRefreshDocuments) {
  EXPECT_FALSE(parseSwaySnapshot("[]", "{}", "{}").has_value());
  EXPECT_FALSE(parseSwaySnapshot("not json", "[]", "{}").has_value());
}

TEST(SwayNumberedProvider, RejectsNoncanonicalNamesZeroAndDuplicateNumbers) {
  for (const auto& name : {"dev", "2:web", "0", "02", "+2", "3"}) {
    const QJsonArray workspaces{QJsonObject{{"name", name}, {"num", 2}}};
    const auto refresh = parseSwayRefresh(QJsonDocument(workspaces).toJson(), "[]", "{}");
    ASSERT_TRUE(refresh);
    EXPECT_FALSE(refresh->numbered.eligible) << name;
  }
  const auto duplicate = parseSwayRefresh(R"([{"id":1,"name":"2","num":2},{"id":2,"name":"2","num":2}])", "[]", "{}");
  ASSERT_TRUE(duplicate);
  EXPECT_FALSE(duplicate->numbered.eligible);
  const auto sparse = parseSwayRefresh(R"([{"id":1,"name":"1","num":1},{"id":2,"name":"8","num":8}])", "[]", "{}");
  ASSERT_TRUE(sparse);
  EXPECT_TRUE(sparse->numbered.eligible);
  EXPECT_EQ(sparse->numbered.assignments.value("2"), 8);
  const auto empty = parseSwayRefresh("[]", "[]", "{}");
  ASSERT_TRUE(empty);
  EXPECT_TRUE(empty->numbered.eligible);
}

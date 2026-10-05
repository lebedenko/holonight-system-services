#include "HyprlandBackend.h"

#include <QCoreApplication>
#include <QSignalSpy>

#include <gtest/gtest.h>

namespace {
class FakeHyprlandTransport final : public HyprlandIpcTransport {
 public:
  void connectEventStream() override { ++connect_count; }
  bool runCommand(const QByteArray& command, CommandCompletePredicate is_complete = {}) override {
    Q_UNUSED(is_complete)
    commands.append(command);
    running = submit_succeeds;
    return submit_succeeds;
  }
  [[nodiscard]] bool hasRunningCommand() const override { return running; }
  void connectStream() { emit eventStreamConnected(); }
  void sendEvent(const QByteArray& event) { emit eventLineReceived(event); }
  void finish(const QByteArray& response, bool success = true) {
    running = false;
    emit commandFinished(response, success);
  }

  int connect_count{0};
  QList<QByteArray> commands;
  bool running{false};
  bool submit_succeeds{true};
};

void processDeferred() { QCoreApplication::processEvents(); }

void finishRefresh(FakeHyprlandTransport* transport, const QByteArray& clients = QByteArrayLiteral("[]")) {
  transport->finish(QByteArrayLiteral(R"([{"name":"DP-1","focused":true,"activeWorkspace":{"id":1}}])"));
  transport->finish(QByteArrayLiteral(R"([{"id":1,"name":"1"}])"));
  transport->finish(clients);
}

QByteArray client(const QString& address, quint32 pid, const QString& title, int workspace = 1) {
  return QStringLiteral(
             R"([{"address":"%1","class":"app","title":"%2","pid":%3,"workspace":{"id":%4},"focusHistoryID":0}])")
      .arg(address, title, QString::number(pid), QString::number(workspace))
      .toUtf8();
}
}  // namespace

TEST(HyprlandBackend, PublishesCompleteRefreshOnlyAfterAllResponses) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);

  backend.start();
  fake->connectStream();
  processDeferred();
  ASSERT_EQ(fake->commands, QList<QByteArray>{QByteArrayLiteral("j/monitors")});
  finishRefresh(fake,
                QByteArrayLiteral(
                    R"([{"address":"0x1","class":"kitty","title":"shell","workspace":{"id":1},"focusHistoryID":0}])"));

  ASSERT_EQ(snapshots.count(), 1);
  const auto snapshot = qvariant_cast<CompositorSnapshot>(snapshots.first().first());
  EXPECT_TRUE(snapshot.connected);
  EXPECT_TRUE(snapshot.capabilities.window_activation);
  ASSERT_EQ(snapshot.workspaces.size(), 1);
  EXPECT_TRUE(snapshot.workspaces.first().occupied.value_or(false));
  EXPECT_EQ(snapshot.active_windows.value(QStringLiteral("DP-1")).title, QStringLiteral("shell"));
}

TEST(HyprlandBackend, DoesNotPublishWindowActivationBeforeValidRefresh) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);

  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Missing);
  fake->connectStream();
  processDeferred();
  fake->finish(QByteArrayLiteral("[]"));
  fake->finish(QByteArrayLiteral("not-json"));
  fake->finish(QByteArrayLiteral("[]"));

  ASSERT_EQ(snapshots.count(), 1);
  EXPECT_FALSE(qvariant_cast<CompositorSnapshot>(snapshots.first().first()).capabilities.window_activation);
}

TEST(HyprlandBackend, ResolvesLineageAndExactTitleToAddress) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, QByteArrayLiteral(R"([
    {"address":"0xchild-a","class":"app","title":"First","pid":42,"workspace":{"id":1}},
    {"address":"0xchild-b","class":"app","title":"Wanted","pid":42,"workspace":{"id":1}},
    {"address":"0xparent","class":"app","title":"Wanted","pid":7,"workspace":{"id":1}}
  ])"));

  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42, 7}, .title_hint = QStringLiteral("Wanted")}),
            WindowActivationResult::Accepted);
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch focuswindow address:0xchild-b"));
}

TEST(HyprlandBackend, ReportsMissingAndAmbiguousWithoutSubmitting) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, QByteArrayLiteral(R"([
    {"address":"0xa","class":"app","title":"A","pid":42,"workspace":{"id":1}},
    {"address":"0xb","class":"app","title":"B","pid":42,"workspace":{"id":1}}
  ])"));
  const qsizetype command_count = fake->commands.size();

  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {7}}), WindowActivationResult::Missing);
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Ambiguous);
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {0}}), WindowActivationResult::InvalidRequest);
  EXPECT_EQ(fake->commands.size(), command_count);
}

TEST(HyprlandBackend, ImmediateSubmissionFailureReturnsFailed) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, client(QStringLiteral("0xa"), 42, QStringLiteral("A")));
  fake->submit_succeeds = false;

  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Failed);
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch focuswindow address:0xa"));
  EXPECT_TRUE(
      qvariant_cast<CompositorSnapshot>(snapshots.last().first()).diagnostic.contains(QStringLiteral("transport")));
}

TEST(HyprlandBackend, FallsBackToLuaWindowActivationAndReportsRejection) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, client(QStringLiteral("0xa"), 42, QStringLiteral("Private title")));

  ASSERT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Accepted);
  fake->finish(QByteArrayLiteral("error: legacy dispatcher unsupported"));
  ASSERT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch hl.dsp.focus({ window = \"address:0xa\" })"));
  fake->finish(QByteArrayLiteral("error: denied"));
  processDeferred();
  EXPECT_TRUE(
      qvariant_cast<CompositorSnapshot>(snapshots.last().first()).diagnostic.contains(QStringLiteral("rejected")));
  EXPECT_FALSE(
      qvariant_cast<CompositorSnapshot>(snapshots.last().first()).diagnostic.contains(QStringLiteral("Private")));
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("j/monitors"));
}

TEST(HyprlandBackend, AcceptedTransportFailurePublishesDiagnosticAndRefreshes) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, client(QStringLiteral("0xa"), 42, QStringLiteral("A")));

  ASSERT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Accepted);
  fake->finish({}, false);
  processDeferred();
  EXPECT_TRUE(
      qvariant_cast<CompositorSnapshot>(snapshots.last().first()).diagnostic.contains(QStringLiteral("transport")));
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("j/monitors"));
}

TEST(HyprlandBackend, QueuesOneWindowWithoutOverwritingIt) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, QByteArrayLiteral(R"([
    {"address":"0xa","class":"app","title":"A","pid":1,"workspace":{"id":1}},
    {"address":"0xb","class":"app","title":"B","pid":2,"workspace":{"id":1}},
    {"address":"0xc","class":"app","title":"C","pid":3,"workspace":{"id":1}}
  ])"));

  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {1}}), WindowActivationResult::Accepted);
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {2}}), WindowActivationResult::Accepted);
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {3}}), WindowActivationResult::Busy);
  fake->finish(QByteArrayLiteral("ok"));
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch focuswindow address:0xb"));
}

TEST(HyprlandBackend, QueuedWindowRetainsAddressAcrossRefresh) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, client(QStringLiteral("0xold"), 42, QStringLiteral("A")));
  fake->sendEvent(QByteArrayLiteral("workspace>>2"));
  processDeferred();

  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Accepted);
  finishRefresh(fake, client(QStringLiteral("0xnew"), 42, QStringLiteral("A")));
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch focuswindow address:0xold"));
}

TEST(HyprlandBackend, DrainsWindowThenWorkspaceThenRefresh) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  finishRefresh(fake, client(QStringLiteral("0xa"), 42, QStringLiteral("A")));
  backend.activateNumberedSlot(4);
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Accepted);
  fake->sendEvent(QByteArrayLiteral("workspace>>2"));

  fake->finish(QByteArrayLiteral("ok"));
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch focuswindow address:0xa"));
  fake->finish(QByteArrayLiteral("ok"));
  processDeferred();
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("j/monitors"));
}

TEST(HyprlandBackend, DiscardsPartialRefreshOnFailure) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);
  fake->connectStream();
  processDeferred();
  fake->finish(QByteArrayLiteral("[]"));
  fake->finish({}, false);

  ASSERT_EQ(snapshots.count(), 1);
  EXPECT_FALSE(qvariant_cast<CompositorSnapshot>(snapshots.first().first()).connected);
}

TEST(HyprlandBackend, CoalescesDirtyRefreshAndQueuesActivation) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  fake->sendEvent(QByteArrayLiteral("workspace>>2"));
  fake->sendEvent(QByteArrayLiteral("openwindow>>a,2,kitty,title"));
  backend.activateNumberedSlot(4);
  finishRefresh(fake);

  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch workspace 4"));
}

TEST(HyprlandBackend, FallsBackToLuaActivationAndRefreshes) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  backend.activateNumberedSlot(4);
  ASSERT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch workspace 4"));
  fake->finish(QByteArrayLiteral("error: unsupported dispatcher"));
  ASSERT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch hl.dsp.focus({ workspace = 4 })"));
  fake->finish(QByteArrayLiteral("ok"));
  processDeferred();
  EXPECT_EQ(fake->commands.last(), QByteArrayLiteral("j/monitors"));
}

TEST(HyprlandBackend, ActivatesSpecialWorkspaceByName) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));

  fake->connectStream();
  processDeferred();
  fake->finish(R"([{"name":"DP-1","focused":true,"activeWorkspace":{"id":1}}])");
  fake->finish(R"([{"id":-98,"name":"special:magic"}])");
  fake->finish("[]");
  backend.activateSpecialWorkspace(QStringLiteral("special:magic"));

  ASSERT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch togglespecialworkspace magic"));
  fake->finish(QByteArrayLiteral("error: unsupported dispatcher"));
  ASSERT_EQ(fake->commands.last(), QByteArrayLiteral("dispatch hl.dsp.workspace.toggle_special(\"magic\")"));
}

TEST(HyprlandBackend, ProjectsVisibleSpecialWorkspaceFromMonitorState) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);
  fake->connectStream();
  processDeferred();
  fake->finish(QByteArrayLiteral(
      R"([{"name":"DP-5","focused":true,"activeWorkspace":{"id":6},"specialWorkspace":{"id":-98,"name":"special:magic"}}])"));
  fake->finish(QByteArrayLiteral(R"([{"id":6,"name":"6"},{"id":-98,"name":"special:magic"}])"));
  fake->sendEvent(QByteArrayLiteral("urgent>>0x1"));
  fake->finish(QByteArrayLiteral(
      R"([{"address":"0x1","class":"teams","title":"Calendar","workspace":{"id":-98},"focusHistoryID":0}])"));

  ASSERT_EQ(snapshots.count(), 1);
  const auto snapshot = qvariant_cast<CompositorSnapshot>(snapshots.first().first());
  EXPECT_EQ(snapshot.workspaces.size(), 1);
  EXPECT_TRUE(snapshot.occupied_outputs.value("DP-5"));
  ASSERT_EQ(backend.specialWorkspaces().size(), 1);
  const auto special = backend.specialWorkspaces().first().toMap();
  EXPECT_TRUE(special.value("active").toBool());
  EXPECT_FALSE(special.value("urgent").toBool());
  EXPECT_EQ(special.value("monitorNames").toStringList(), QStringList{"DP-5"});
}

TEST(HyprlandBackend, ClearsUrgencyWhenEventAddressOmitsClientPrefix) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  QSignalSpy snapshots(&backend, &CompositorBackend::snapshotReady);
  fake->connectStream();
  processDeferred();
  fake->sendEvent(QByteArrayLiteral("urgent>>1"));
  finishRefresh(fake,
                QByteArrayLiteral(
                    R"([{"address":"0x1","class":"kitty","title":"shell","workspace":{"id":1},"focusHistoryID":0}])"));

  fake->sendEvent(QByteArrayLiteral("workspace>>2"));
  processDeferred();
  fake->finish(QByteArrayLiteral(R"([{"name":"DP-1","focused":true,"activeWorkspace":{"id":2}}])"));
  fake->finish(QByteArrayLiteral(R"([{"id":1,"name":"1"},{"id":2,"name":"2"}])"));
  fake->finish(QByteArrayLiteral(
      R"([{"address":"0x1","class":"kitty","title":"shell","workspace":{"id":1},"focusHistoryID":0}])"));

  ASSERT_EQ(snapshots.count(), 2);
  const auto snapshot = qvariant_cast<CompositorSnapshot>(snapshots.last().first());
  const auto workspace = std::ranges::find(snapshot.workspaces, QStringLiteral("1"), &CompositorWorkspace::id);
  ASSERT_NE(workspace, snapshot.workspaces.end());
  EXPECT_FALSE(workspace->urgent);
}

TEST(HyprlandBackend, DisconnectClearsPrivateSpecialAndNumberedState) {
  auto transport = std::make_unique<FakeHyprlandTransport>();
  auto* fake = transport.get();
  HyprlandBackend backend(std::move(transport));
  fake->connectStream();
  processDeferred();
  fake->finish(R"([{"name":"DP-1","focused":true,"activeWorkspace":{"id":1}}])");
  fake->finish(R"([{"id":1,"name":"1"},{"id":-98,"name":"special:magic"}])");
  fake->finish("[]");
  ASSERT_TRUE(backend.numberedWorkspaces().eligible);
  ASSERT_EQ(backend.specialWorkspaces().size(), 1);
  emit fake->eventStreamDisconnected();
  EXPECT_FALSE(backend.numberedWorkspaces().eligible);
  EXPECT_TRUE(backend.specialWorkspaces().isEmpty());
  fake->connectStream();
  processDeferred();
  finishRefresh(fake);
  EXPECT_TRUE(backend.numberedWorkspaces().eligible);
}

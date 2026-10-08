#include "CompositorFactory.h"
#include "SwayBackend.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <functional>
#include <gtest/gtest.h>
#include <memory>

namespace {
bool waitUntil(const std::function<bool()>& predicate) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (predicate()) {
      return true;
    }
  }
  return false;
}

// GTest fixtures expose state to generated subclasses; retain the project's private-member suffix
// convention while allowing TEST_F bodies to address the fixture directly.
// NOLINTBEGIN(readability-identifier-naming, cppcoreguidelines-non-private-member-variables-in-classes)
class SwayBackendTest : public testing::Test {
 protected:
  void SetUp() override {
    socket_path_ = QStringLiteral("holonight-sway-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    ASSERT_TRUE(server_.listen(socket_path_)) << server_.errorString().toStdString();
    backend_ = std::make_unique<SwayBackend>(socket_path_);
    backend_->start();
    acceptConnections();
  }

  void acceptConnections() {
    first_decoder_ = {};
    second_decoder_ = {};
    queued_request_frames_.clear();
    ASSERT_TRUE(waitUntil([this] { return server_.hasPendingConnections(); }));
    first_ = server_.nextPendingConnection();
    ASSERT_TRUE(waitUntil([this] { return server_.hasPendingConnections(); }));
    second_ = server_.nextPendingConnection();
    ASSERT_TRUE(waitUntil([this] { return first_->bytesAvailable() > 0 || second_->bytesAvailable() > 0; }));
    const QList<SwayIpcFrame> first_frames = readFrames(first_);
    const QList<SwayIpcFrame> second_frames = readFrames(second_);
    if (!first_frames.isEmpty() && first_frames.first().type == 2) {
      subscription_ = first_;
      request_ = second_;
      queued_request_frames_ = second_frames;
    } else {
      subscription_ = second_;
      request_ = first_;
      queued_request_frames_ = first_frames;
    }
    writeFrame(subscription_, 2, R"({"success":true})");
  }

  void TearDown() override {
    request_->disconnectFromServer();
    subscription_->disconnectFromServer();
    waitUntil([this] {
      return request_->state() == QLocalSocket::UnconnectedState &&
             subscription_->state() == QLocalSocket::UnconnectedState;
    });
    backend_.reset();
  }

  QList<SwayIpcFrame> readFrames(QLocalSocket* socket) {
    SwayIpcDecoder& decoder = socket == first_ ? first_decoder_ : second_decoder_;
    decoder.append(socket->readAll());
    return decoder.takeFrames();
  }

  SwayIpcFrame nextRequest() {
    if (!queued_request_frames_.isEmpty()) {
      return queued_request_frames_.takeFirst();
    }
    EXPECT_TRUE(waitUntil([this] { return request_->bytesAvailable() > 0; }));
    const auto frames = readFrames(request_);
    EXPECT_FALSE(frames.isEmpty());
    return frames.value(0);
  }

  static void writeFrame(QLocalSocket* socket, quint32 type, const QByteArray& payload) {
    socket->write(encodeSwayIpcFrame(type, payload));
    socket->flush();
  }

  void finishRefresh(const QByteArray& tree,
                     const QByteArray& workspaces = R"([{"num":1,"name":"1","output":"DP-1"}])") {
    EXPECT_EQ(nextRequest().type, 1U);
    writeFrame(request_, 1, workspaces);
    EXPECT_EQ(nextRequest().type, 3U);
    writeFrame(request_, 3, R"([{"name":"DP-1","focused":true}])");
    EXPECT_EQ(nextRequest().type, 4U);
    writeFrame(request_, 4, tree);
    waitUntil([] { return false; });
  }

  QString socket_path_;
  QLocalServer server_;
  std::unique_ptr<SwayBackend> backend_;
  QLocalSocket* first_{nullptr};
  QLocalSocket* second_{nullptr};
  QLocalSocket* request_{nullptr};
  QLocalSocket* subscription_{nullptr};
  SwayIpcDecoder first_decoder_;
  SwayIpcDecoder second_decoder_;
  QList<SwayIpcFrame> queued_request_frames_;
};
// NOLINTEND(readability-identifier-naming, cppcoreguidelines-non-private-member-variables-in-classes)
}  // namespace

TEST(SwayBackendDisconnected, ValidatesBeforeReportingDisconnected) {
  SwayBackend backend(QStringLiteral("/nonexistent/sway.sock"));
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {0}}), WindowActivationResult::InvalidRequest);
  EXPECT_EQ(backend.requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Disconnected);
}

TEST_F(SwayBackendTest, PublishesCapabilityAndActivatesResolvedContainer) {
  QSignalSpy spy(backend_.get(), &CompositorBackend::snapshotReady);
  finishRefresh(R"({"type":"root","nodes":[
    {"type":"con","id":101,"pid":42,"name":"Other","nodes":[],"floating_nodes":[]},
    {"type":"con","id":202,"pid":42,"name":"Wanted","nodes":[],"floating_nodes":[]},
    {"type":"con","id":303,"pid":7,"name":"Wanted","nodes":[],"floating_nodes":[]}
  ],"floating_nodes":[]})");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(qvariant_cast<CompositorSnapshot>(spy.first().first()).capabilities.window_activation);
  EXPECT_TRUE(backend_->numberedWorkspaces().eligible);

  EXPECT_EQ(backend_->requestWindowActivation({.process_lineage = {0}}), WindowActivationResult::InvalidRequest);
  EXPECT_EQ(backend_->requestWindowActivation({.process_lineage = {99}}), WindowActivationResult::Missing);
  EXPECT_EQ(backend_->requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Ambiguous);
  ASSERT_EQ(backend_->requestWindowActivation({.process_lineage = {42, 7}, .title_hint = QStringLiteral("Wanted")}),
            WindowActivationResult::Accepted);
  const SwayIpcFrame command = nextRequest();
  EXPECT_EQ(command.type, 0U);
  EXPECT_EQ(command.payload, QByteArrayLiteral("[con_id=202] focus"));
}

TEST_F(SwayBackendTest, QueuesOneCapturedWindowAndDrainsBeforeWorkspaceAndRefresh) {
  QSignalSpy spy(backend_.get(), &CompositorBackend::snapshotReady);
  finishRefresh(R"({"type":"root","nodes":[
    {"type":"con","id":101,"pid":1,"name":"A"},{"type":"con","id":202,"pid":2,"name":"B"},
    {"type":"con","id":303,"pid":3,"name":"C"}],"floating_nodes":[]})");

  backend_->activateNumberedSlot(4);
  EXPECT_EQ(backend_->requestWindowActivation({.process_lineage = {2}}), WindowActivationResult::Accepted);
  EXPECT_EQ(backend_->requestWindowActivation({.process_lineage = {3}}), WindowActivationResult::Busy);
  writeFrame(subscription_, (1U << 31U), R"({})");
  EXPECT_EQ(nextRequest().payload, QByteArrayLiteral("workspace --no-auto-back-and-forth number 4"));
  writeFrame(request_, 0, R"([{"success":true}])");
  EXPECT_EQ(nextRequest().payload, QByteArrayLiteral("[con_id=202] focus"));
  writeFrame(request_, 0, R"([{"success":true}])");
  EXPECT_EQ(nextRequest().type, 1U);
}

TEST_F(SwayBackendTest, RetainsCapturedContainerAcrossRefreshAndDrainsWindowBeforeWorkspace) {
  QSignalSpy spy(backend_.get(), &CompositorBackend::snapshotReady);
  finishRefresh(R"({"type":"root","nodes":[{"type":"con","id":101,"pid":42,"name":"A"}],"floating_nodes":[]})");

  writeFrame(subscription_, (1U << 31U), R"({})");
  EXPECT_EQ(nextRequest().type, 1U);
  writeFrame(request_, 1, R"([{"num":1,"name":"1","output":"DP-1"}])");
  EXPECT_EQ(nextRequest().type, 3U);
  writeFrame(request_, 3, R"([{"name":"DP-1","focused":true}])");
  EXPECT_EQ(nextRequest().type, 4U);
  EXPECT_EQ(backend_->requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Accepted);
  backend_->activateNumberedSlot(4);
  writeFrame(request_, 4,
             R"({"type":"root","nodes":[{"type":"con","id":999,"pid":42,"name":"A"}],"floating_nodes":[]})");

  EXPECT_EQ(nextRequest().payload, QByteArrayLiteral("[con_id=101] focus"));
  writeFrame(request_, 0, R"([{"success":true}])");
  EXPECT_EQ(nextRequest().payload, QByteArrayLiteral("workspace --no-auto-back-and-forth number 4"));
  writeFrame(request_, 0, R"([{"success":true}])");
  EXPECT_EQ(nextRequest().type, 1U);
}

TEST_F(SwayBackendTest, RejectionPublishesBoundedDiagnosticAndSchedulesRefresh) {
  QSignalSpy spy(backend_.get(), &CompositorBackend::snapshotReady);
  finishRefresh(
      R"({"type":"root","nodes":[{"type":"con","id":9,"pid":42,"name":"Private title"}],"floating_nodes":[]})");
  ASSERT_EQ(backend_->requestWindowActivation({.process_lineage = {42}}), WindowActivationResult::Accepted);
  nextRequest();
  writeFrame(request_, 0, R"([{"success":false,"error":"Private title"}])");
  EXPECT_TRUE(waitUntil([&spy] { return spy.count() >= 2; }));
  const QString diagnostic = qvariant_cast<CompositorSnapshot>(spy.last().first()).diagnostic;
  EXPECT_TRUE(diagnostic.contains(QStringLiteral("rejected")));
  EXPECT_FALSE(diagnostic.contains(QStringLiteral("Private")));
  EXPECT_EQ(nextRequest().type, 1U);
}

TEST_F(SwayBackendTest, PreservesExactNamesAndQueuesNumericActivationDuringRefresh) {
  const QStringList names{"dev:\"web\\tools", "0", "02", "2:web", "+2", "2147483648"};
  QJsonArray workspaces;
  for (const auto& name : names) {
    workspaces.append(QJsonObject{{"name", name}, {"num", -1}});
  }
  backend_->activateNumberedSlot(5);
  finishRefresh(R"({"type":"root","nodes":[],"floating_nodes":[]})", QJsonDocument(workspaces).toJson());
  EXPECT_EQ(nextRequest().payload, QByteArrayLiteral("workspace --no-auto-back-and-forth number 5"));
  for (const auto& name : names) {
    backend_->activateWorkspace("name:" + name);
    writeFrame(request_, 0, R"([{"success":true}])");
    EXPECT_EQ(nextRequest().payload, (QStringLiteral("workspace --no-auto-back-and-forth \"") +
                                      escapeSwayWorkspaceName(name) + QStringLiteral("\""))
                                         .toUtf8());
  }
  writeFrame(request_, 0, R"([{"success":true}])");
  EXPECT_EQ(nextRequest().type, 1U);
}

TEST_F(SwayBackendTest, ResolvesQueuedExistingIdAfterRenameInRefresh) {
  finishRefresh("{}", R"([{"id":7,"name":"before","num":-1}])");
  writeFrame(subscription_, (1U << 31U), "{}");
  EXPECT_EQ(nextRequest().type, 1U);
  backend_->activateWorkspace("7");
  writeFrame(request_, 1, R"([{"id":7,"name":"after","num":-1}])");
  EXPECT_EQ(nextRequest().type, 3U);
  writeFrame(request_, 3, "[]");
  EXPECT_EQ(nextRequest().type, 4U);
  writeFrame(request_, 4, "{}");
  EXPECT_EQ(nextRequest().payload, QByteArrayLiteral("workspace --no-auto-back-and-forth \"after\""));
}

TEST_F(SwayBackendTest, ReconnectsSelectedIntegrationAndClearsPublishedState) {
  QSignalSpy snapshots(backend_.get(), &CompositorBackend::snapshotReady);
  const QByteArray title_tree = R"({"type":"root","layout":"splith","fullscreen_mode":0,"nodes":[
    {"type":"con","layout":"none","id":101,"pid":42,"app_id":"viewer","shell":"xdg_shell","fullscreen_mode":0,
     "nodes":[],"floating_nodes":[],"deco_rect":{"x":0,"y":0,"width":640,"height":22}}
  ],"floating_nodes":[]})";
  finishRefresh(title_tree);
  ASSERT_TRUE(backend_->numberedWorkspaces().eligible);
  EXPECT_EQ(qvariant_cast<CompositorSnapshot>(snapshots.last().first()).windows.size(), 1);
  subscription_->disconnectFromServer();
  ASSERT_TRUE(waitUntil([&] { return snapshots.count() > 1; }));
  EXPECT_FALSE(qvariant_cast<CompositorSnapshot>(snapshots.last().first()).connected);
  EXPECT_FALSE(backend_->numberedWorkspaces().eligible);
  EXPECT_EQ(qvariant_cast<CompositorSnapshot>(snapshots.last().first()).windows.size(), 0);
  QTest::qWait(1100);
  acceptConnections();
  finishRefresh(title_tree);
  EXPECT_TRUE(qvariant_cast<CompositorSnapshot>(snapshots.last().first()).connected);
  EXPECT_TRUE(backend_->numberedWorkspaces().eligible);
  EXPECT_EQ(qvariant_cast<CompositorSnapshot>(snapshots.last().first()).windows.size(), 1);
}

TEST_F(SwayBackendTest, WindowEventsCoalesceAndPublishChangedTitles) {
  QSignalSpy snapshots(backend_.get(), &CompositorBackend::snapshotReady);
  const QByteArray tree = R"({"type":"root","nodes":[
    {"type":"con","id":101,"pid":42,"app_id":"viewer","shell":"xdg_shell","name":"before",
     "nodes":[],"floating_nodes":[]}
  ]})";
  finishRefresh(tree);
  for (int i = 0; i < 3; ++i) {
    writeFrame(subscription_, (1U << 31U) | 3U, "{}");
  }
  auto updated = tree;
  updated.replace("before", "after");
  finishRefresh(updated);
  ASSERT_TRUE(waitUntil([&] { return snapshots.count() >= 2; }));
  // An event received during the refresh can queue one follow-up refresh.
  if (waitUntil([&] { return request_->bytesAvailable() > 0; })) {
    finishRefresh(updated);
  }
  EXPECT_LE(snapshots.count(), 3);
  const auto snapshot = qvariant_cast<CompositorSnapshot>(snapshots.last().first());
  ASSERT_EQ(snapshot.windows.size(), 1);
  EXPECT_EQ(snapshot.windows.first().title, "after");
  EXPECT_EQ(snapshot.windows.first().pid, 42U);
  EXPECT_EQ(request_->bytesAvailable(), 0);
}

#include "HyprlandIpcClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

namespace {

class TestSocket : public QLocalSocket {
 public:
  using QLocalSocket::QLocalSocket;

  void emitConnected() { Q_EMIT connected(); }
  void emitDisconnected() { Q_EMIT disconnected(); }
  void emitError(QLocalSocket::LocalSocketError error) { Q_EMIT errorOccurred(error); }
  void emitReadyRead() { Q_EMIT readyRead(); }
};

class ScopedHyprlandSocketEnv {
 public:
  explicit ScopedHyprlandSocketEnv(const QByteArray& signature)
      : old_runtime_dir_(qgetenv("XDG_RUNTIME_DIR")), old_signature_(qgetenv("HYPRLAND_INSTANCE_SIGNATURE")) {
    EXPECT_TRUE(temp_dir_.isValid());
    qputenv("XDG_RUNTIME_DIR", temp_dir_.path().toLocal8Bit());
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature);
    EXPECT_TRUE(QDir().mkpath(temp_dir_.path() + QStringLiteral("/hypr/") + QString::fromLocal8Bit(signature)));
  }

  ~ScopedHyprlandSocketEnv() {
    restoreEnv("XDG_RUNTIME_DIR", old_runtime_dir_);
    restoreEnv("HYPRLAND_INSTANCE_SIGNATURE", old_signature_);
  }

  ScopedHyprlandSocketEnv(const ScopedHyprlandSocketEnv&) = delete;
  ScopedHyprlandSocketEnv& operator=(const ScopedHyprlandSocketEnv&) = delete;
  ScopedHyprlandSocketEnv(ScopedHyprlandSocketEnv&&) = delete;
  ScopedHyprlandSocketEnv& operator=(ScopedHyprlandSocketEnv&&) = delete;

 private:
  static void restoreEnv(const char* name, const QByteArray& value) {
    if (value.isNull()) {
      qunsetenv(name);
    } else {
      qputenv(name, value);
    }
  }

  QTemporaryDir temp_dir_{QStringLiteral("/tmp/hnipc-XXXXXX")};
  QByteArray old_runtime_dir_;
  QByteArray old_signature_;
};

}  // namespace

TEST(HyprlandIpcClient, RunCommandWarnsAndDropsWhenSignatureUnset) {
  ScopedHyprlandSocketEnv env{QByteArray()};
  HyprlandIpcClient client(QStringLiteral("test"));

  QTest::ignoreMessage(QtWarningMsg,
                       R"("test" HYPRLAND_INSTANCE_SIGNATURE not set; dropping command "set_workspace 3")");
  EXPECT_FALSE(client.runCommand(QByteArrayLiteral("set_workspace 3")));
}

TEST(HyprlandIpcClient, RunCommandCompletesWithoutWarningWhenSignatureSet) {
  ScopedHyprlandSocketEnv env("run-command-ok");
  const QString socket_path = HyprlandIpcClient::commandSocketPath();

  QLocalServer server;
  QLocalServer::removeServer(socket_path);
  ASSERT_TRUE(server.listen(socket_path)) << qPrintable(server.errorString());

  HyprlandIpcClient client(QStringLiteral("test"));
  QSignalSpy finished(&client, &HyprlandIpcClient::commandFinished);

  EXPECT_TRUE(client.runCommand(QByteArrayLiteral("j/activeworkspace")));

  if (!server.hasPendingConnections()) {
    ASSERT_TRUE(server.waitForNewConnection(1000));
  }
  QLocalSocket* socket = server.nextPendingConnection();
  ASSERT_NE(socket, nullptr);

  ASSERT_TRUE(socket->waitForReadyRead(1000));
  EXPECT_EQ(socket->readAll(), QByteArrayLiteral("j/activeworkspace"));
  socket->write(R"({"id":3})");
  socket->flush();
  socket->disconnectFromServer();

  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
  EXPECT_EQ(finished.at(0).at(0).toByteArray(), QByteArrayLiteral(R"({"id":3})"));
  EXPECT_TRUE(finished.at(0).at(1).toBool());
}

TEST(HyprlandIpcClient, BuildsSocketPathsFromRuntimeEnvironment) {
  ScopedHyprlandSocketEnv env("ipc-paths");

  EXPECT_TRUE(HyprlandIpcClient::socketBasePath().endsWith(QStringLiteral("/hypr/")));
  EXPECT_TRUE(HyprlandIpcClient::eventSocketPath().endsWith(QStringLiteral("/hypr/ipc-paths/.socket2.sock")));
  EXPECT_TRUE(HyprlandIpcClient::commandSocketPath().endsWith(QStringLiteral("/hypr/ipc-paths/.socket.sock")));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(HyprlandIpcClient, EmitsCompleteEventLinesFromPartialChunks) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  QString socket_path = temp_dir.filePath("event_socket");

  QLocalServer server;
  QLocalServer::removeServer(socket_path);
  ASSERT_TRUE(server.listen(socket_path)) << qPrintable(server.errorString());

  HyprlandIpcClient client(QStringLiteral("test"), socket_path, QString(), false);
  QSignalSpy connected(&client, &HyprlandIpcClient::eventStreamConnected);
  QSignalSpy lines(&client, &HyprlandIpcClient::eventLineReceived);
  client.connectEventStream();

  if (!server.hasPendingConnections()) {
    ASSERT_TRUE(server.waitForNewConnection(1000));
  }
  QLocalSocket* socket = server.nextPendingConnection();
  ASSERT_NE(socket, nullptr);
  QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);

  socket->write("workspace>>");
  socket->flush();
  QTest::qWait(20);
  EXPECT_EQ(lines.count(), 0);

  socket->write("3\nactivewindow>>kitty,term\nfocused");
  socket->flush();
  QTRY_COMPARE_WITH_TIMEOUT(lines.count(), 2, 1000);
  EXPECT_EQ(lines.at(0).at(0).toByteArray(), QByteArrayLiteral("workspace>>3"));
  EXPECT_EQ(lines.at(1).at(0).toByteArray(), QByteArrayLiteral("activewindow>>kitty,term"));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(HyprlandIpcClient, CompletesCommandWhenPredicateAcceptsResponse) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  QString socket_path = temp_dir.filePath("cmd_socket");

  QLocalServer server;
  QLocalServer::removeServer(socket_path);
  ASSERT_TRUE(server.listen(socket_path)) << qPrintable(server.errorString());

  HyprlandIpcClient client(QStringLiteral("test"), QString(), socket_path, false);
  QSignalSpy finished(&client, &HyprlandIpcClient::commandFinished);
  ASSERT_TRUE(client.runCommand(QByteArrayLiteral("j/devices"),
                                [](const QByteArray& response) { return response.contains("English"); }));

  if (!server.hasPendingConnections()) {
    ASSERT_TRUE(server.waitForNewConnection(1000));
  }
  QLocalSocket* socket = server.nextPendingConnection();
  ASSERT_NE(socket, nullptr);

  ASSERT_TRUE(socket->waitForReadyRead(1000));
  EXPECT_EQ(socket->readAll(), QByteArrayLiteral("j/devices"));

  socket->write(R"({"keyboards":[{"active_keymap":"English)");
  socket->flush();
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 0, 100);
  socket->write(QByteArrayLiteral(" (US)\"}]}"));
  socket->flush();

  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
  EXPECT_TRUE(finished.at(0).at(0).toByteArray().contains("English"));
  EXPECT_TRUE(finished.at(0).at(1).toBool());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(HyprlandIpcClient, CompletesCommandFromPeerClose) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  QString socket_path = temp_dir.filePath("cmd_socket");

  QLocalServer server;
  QLocalServer::removeServer(socket_path);
  ASSERT_TRUE(server.listen(socket_path)) << qPrintable(server.errorString());

  HyprlandIpcClient client(QStringLiteral("test"), QString(), socket_path, false);
  QSignalSpy finished(&client, &HyprlandIpcClient::commandFinished);
  ASSERT_TRUE(client.runCommand(QByteArrayLiteral("j/activeworkspace")));

  if (!server.hasPendingConnections()) {
    ASSERT_TRUE(server.waitForNewConnection(1000));
  }
  QLocalSocket* socket = server.nextPendingConnection();
  ASSERT_NE(socket, nullptr);

  ASSERT_TRUE(socket->waitForReadyRead(1000));
  socket->write(R"({"id":4})");
  socket->flush();
  socket->disconnectFromServer();

  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 1000);
  EXPECT_EQ(finished.at(0).at(0).toByteArray(), QByteArrayLiteral(R"({"id":4})"));
  EXPECT_TRUE(finished.at(0).at(1).toBool());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(HyprlandIpcClient, ReconnectsEventStreamAfterInitialFailure) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  QString socket_path = temp_dir.filePath("reconnect_socket");

  HyprlandIpcClient client(QStringLiteral("test"), socket_path, QString(), false);
  client.testSetReconnectDelay(1);

  QSignalSpy connected(&client, &HyprlandIpcClient::eventStreamConnected);
  client.connectEventStream();

  QTest::qWait(10);

  QLocalServer server;
  QLocalServer::removeServer(socket_path);
  ASSERT_TRUE(server.listen(socket_path)) << qPrintable(server.errorString());

  QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
  QLocalSocket* socket = server.nextPendingConnection();
  ASSERT_NE(socket, nullptr);
  QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 1000);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(HyprlandIpcClient, CommandTimeoutFailsOutstandingCommand) {
  QTemporaryDir temp_dir;
  ASSERT_TRUE(temp_dir.isValid());
  QString socket_path = temp_dir.filePath("cmd_socket");

  QLocalServer server;
  QLocalServer::removeServer(socket_path);
  ASSERT_TRUE(server.listen(socket_path)) << qPrintable(server.errorString());

  HyprlandIpcClient client(QStringLiteral("test"), QString(), socket_path, false);
  client.testSetCommandTimeout(10);

  QSignalSpy finished(&client, &HyprlandIpcClient::commandFinished);
  ASSERT_TRUE(client.runCommand(QByteArrayLiteral("j/devices")));

  if (!server.hasPendingConnections()) {
    ASSERT_TRUE(server.waitForNewConnection(1000));
  }
  ASSERT_NE(server.nextPendingConnection(), nullptr);

  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 500);
  EXPECT_FALSE(finished.at(0).at(1).toBool());
}

TEST(HyprlandIpcClient, IgnoresStaleEventSocketSignals) {
  HyprlandIpcClient client(QStringLiteral("test"), QStringLiteral("current-event-socket"), QString(), true);
  auto* active_socket = new TestSocket(&client);
  TestSocket stale_socket;
  client.testSetEventSocket(active_socket);

  QSignalSpy connected(&client, &HyprlandIpcClient::eventStreamConnected);
  QSignalSpy disconnected(&client, &HyprlandIpcClient::eventStreamDisconnected);
  QSignalSpy lines(&client, &HyprlandIpcClient::eventLineReceived);
  QObject::connect(&stale_socket, &QLocalSocket::connected, &client,
                   &HyprlandIpcClient::testHandleEventSocketConnected);
  QObject::connect(&stale_socket, &QLocalSocket::readyRead, &client, &HyprlandIpcClient::testHandleEventSocketReadable);
  QObject::connect(&stale_socket, &QLocalSocket::disconnected, &client,
                   &HyprlandIpcClient::testHandleEventSocketDisconnected);
  QObject::connect(&stale_socket, &QLocalSocket::errorOccurred, &client,
                   &HyprlandIpcClient::testHandleEventSocketError);

  stale_socket.emitConnected();
  stale_socket.emitReadyRead();
  stale_socket.emitDisconnected();
  stale_socket.emitError(QLocalSocket::ServerNotFoundError);
  QCoreApplication::processEvents();

  EXPECT_EQ(connected.count(), 0);
  EXPECT_EQ(disconnected.count(), 0);
  EXPECT_EQ(lines.count(), 0);

  QObject::connect(active_socket, &QLocalSocket::connected, &client,
                   &HyprlandIpcClient::testHandleEventSocketConnected);
  active_socket->emitConnected();
  EXPECT_EQ(connected.count(), 1);
}

TEST(HyprlandIpcClient, IgnoresStaleCommandSocketSignals) {
  HyprlandIpcClient client(QStringLiteral("test"), QString(), QStringLiteral("current-command-socket"), true);
  auto* active_socket = new TestSocket(&client);
  TestSocket stale_socket;
  client.testSetCommandSocket(active_socket);

  QSignalSpy finished(&client, &HyprlandIpcClient::commandFinished);
  QObject::connect(&stale_socket, &QLocalSocket::connected, &client,
                   &HyprlandIpcClient::testHandleCommandSocketConnected);
  QObject::connect(&stale_socket, &QLocalSocket::readyRead, &client,
                   &HyprlandIpcClient::testHandleCommandSocketReadable);
  QObject::connect(&stale_socket, &QLocalSocket::disconnected, &client,
                   &HyprlandIpcClient::testHandleCommandSocketDisconnected);
  QObject::connect(&stale_socket, &QLocalSocket::errorOccurred, &client,
                   &HyprlandIpcClient::testHandleCommandSocketError);

  stale_socket.emitConnected();
  stale_socket.emitReadyRead();
  stale_socket.emitDisconnected();
  stale_socket.emitError(QLocalSocket::ServerNotFoundError);
  QCoreApplication::processEvents();

  EXPECT_EQ(finished.count(), 0);
  EXPECT_TRUE(client.hasRunningCommand());

  QObject::connect(active_socket, &QLocalSocket::disconnected, &client,
                   &HyprlandIpcClient::testHandleCommandSocketDisconnected);
  active_socket->emitDisconnected();

  EXPECT_EQ(finished.count(), 1);
  EXPECT_TRUE(finished.at(0).at(0).toByteArray().isEmpty());
  EXPECT_TRUE(finished.at(0).at(1).toBool());
}

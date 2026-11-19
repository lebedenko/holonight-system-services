#include "SwayInputObserver.h"
#include "SwayIpc.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <gtest/gtest.h>

namespace {
bool waitFor(const std::function<bool()>& predicate) {
  QElapsedTimer timeout;
  timeout.start();
  while (!predicate() && timeout.elapsed() < 2500) {
    QTest::qWait(5);
  }
  return predicate();
}

class ScopedSwaySocket {
 public:
  explicit ScopedSwaySocket(const QByteArray& path) { qputenv("SWAYSOCK", path); }
  ~ScopedSwaySocket() {
    if (previous_.isNull()) {
      qunsetenv("SWAYSOCK");
    } else {
      qputenv("SWAYSOCK", previous_);
    }
  }

 private:
  QByteArray previous_ = qgetenv("SWAYSOCK");
};

SwayIpcFrame nextFrame(QLocalSocket& socket) {
  SwayIpcDecoder decoder;
  QList<SwayIpcFrame> frames;
  EXPECT_TRUE(waitFor([&] {
    if (frames.isEmpty()) {
      decoder.append(socket.readAll());
      frames = decoder.takeFrames();
    }
    return !frames.isEmpty();
  }));
  EXPECT_EQ(frames.size(), 1);
  return frames.value(0);
}

void send(QLocalSocket& socket, quint32 type, const QByteArray& payload) {
  socket.write(encodeSwayIpcFrame(type, payload));
  socket.flush();
}
}  // namespace

TEST(SwayInputObserverTest, SelectsKeyboardCoalescesRefreshesAndRecoversFromDisconnect) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto path = directory.filePath("sway.sock");
  const ScopedSwaySocket environment(path.toUtf8());
  QLocalServer server;
  ASSERT_TRUE(server.listen(path));

  HoloNight::System::SwayInputObserver observer;
  QSignalSpy changes(&observer, &HoloNight::System::SwayInputObserver::layoutNameChanged);
  observer.start();
  observer.start();

  ASSERT_TRUE(waitFor([&] { return server.hasPendingConnections(); }));
  auto* socket = server.nextPendingConnection();
  const auto subscription = nextFrame(*socket);
  EXPECT_EQ(subscription.type, 2U);
  EXPECT_EQ(subscription.payload, R"(["input"])");
  EXPECT_EQ(socket->bytesAvailable(), 0);
  send(*socket, 2, R"({"success":true})");
  EXPECT_EQ(nextFrame(*socket).type, 100U);

  send(*socket, 100, R"([
    {"type":"keyboard","identifier":"z","xkb_layout_names":["English","German"],"xkb_active_layout_name":"German"},
    {"type":"keyboard","identifier":"a","xkb_layout_names":["English","Ukrainian"],"xkb_active_layout_name":"English"},
    {"type":"keyboard","identifier":"0","xkb_layout_names":["German"],"xkb_active_layout_name":"German"},
    {"type":"keyboard","identifier":"unknown","xkb_layout_names":["A","B","C"],"xkb_active_layout_name":null},
    {"type":"pointer","identifier":"mouse","xkb_layout_names":["A","B","C"],"xkb_active_layout_name":"A"}
  ])");
  ASSERT_TRUE(waitFor([&] { return observer.layoutName() == "English"; }));

  send(*socket, 0x80000015, R"({"change":"xkb_layout","input":{"type":"keyboard"}})");
  EXPECT_EQ(nextFrame(*socket).type, 100U);
  send(*socket, 0x80000015, R"({"change":"xkb_keymap","input":{"type":"keyboard"}})");
  send(*socket, 0x80000015, R"({"change":"added","input":{"type":"keyboard"}})");
  send(*socket, 100, R"([{"type":"keyboard","xkb_active_layout_name":"Stale"}])");
  EXPECT_EQ(nextFrame(*socket).type, 100U);
  EXPECT_EQ(observer.layoutName(), "English");
  send(*socket, 100, R"([{"type":"keyboard","xkb_active_layout_name":"Ukrainian"}])");
  ASSERT_TRUE(waitFor([&] { return observer.layoutName() == "Ukrainian"; }));
  EXPECT_EQ(changes.count(), 2);

  send(*socket, 0x80000015, R"({"change":"removed","input":{"type":"keyboard"}})");
  EXPECT_EQ(nextFrame(*socket).type, 100U);
  send(*socket, 100, R"([{"type":"keyboard","xkb_active_layout_name":"German"}])");
  ASSERT_TRUE(waitFor([&] { return observer.layoutName() == "German"; }));

  socket->disconnectFromServer();
  ASSERT_TRUE(waitFor([&] { return observer.layoutName().isEmpty(); }));
  ASSERT_TRUE(waitFor([&] { return server.hasPendingConnections(); }));
  socket = server.nextPendingConnection();
  EXPECT_EQ(nextFrame(*socket).type, 2U);
  send(*socket, 2, R"({"success":true})");
  EXPECT_EQ(nextFrame(*socket).type, 100U);
  send(*socket, 100, R"([{"type":"keyboard","xkb_active_layout_name":"English"}])");
  ASSERT_TRUE(waitFor([&] { return observer.layoutName() == "English"; }));

  send(*socket, 0x80000015, R"({"change":"removed","input":{"type":"keyboard"}})");
  EXPECT_EQ(nextFrame(*socket).type, 100U);
  send(*socket, 100, "[]");
  EXPECT_TRUE(waitFor([&] { return observer.layoutName().isEmpty(); }));
  EXPECT_EQ(changes.count(), 6);
}

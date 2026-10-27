#include "BlueZBackend.h"
#include "BluetoothController.h"

#include <QCoreApplication>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
using namespace HoloNight::System;
namespace {
const QString service = QStringLiteral("org.holonight.TestBlueZ");
const QString adapter = QStringLiteral("/org/bluez/hci0");
const QString device = adapter + QStringLiteral("/dev_01");
const QString adapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString deviceInterface = QStringLiteral("org.bluez.Device1");
class FakeBlueZ : public QDBusVirtualObject {
 public:
  QDBusConnection bus;
  BluetoothObjects objects;
  QStringList calls;
  QString agentPath, client;
  QList<QPair<QString, QString>> agents;
  QList<QDBusMessage> held;
  bool hold = false;
  bool holdSnapshots = false;
  QList<QDBusMessage> snapshots;
  QString failingMember;
  explicit FakeBlueZ(QDBusConnection connection) : bus(connection) {
    objects[QDBusObjectPath(adapter)][adapterInterface] = {
        {"Address", "AA"}, {"Alias", "Computer"}, {"Powered", true}, {"Discovering", true}};
    objects[QDBusObjectPath(device)][deviceInterface] = {
        {"Adapter", QVariant::fromValue(QDBusObjectPath(adapter))}, {"Alias", "Keyboard"}, {"RSSI", -40}};
  }
  QString introspect(const QString&) const override { return {}; }
  bool handleMessage(const QDBusMessage& m, const QDBusConnection&) override {
    if (m.member() == "GetManagedObjects") {
      if (holdSnapshots)
        snapshots.append(m);
      else
        bus.send(m.createReply({QVariant::fromValue(objects)}));
      return true;
    }
    calls.append(m.member());
    if (m.member() == failingMember) {
      bus.send(m.createErrorReply("org.bluez.Error.Failed", "Test failure"));
      return true;
    }
    if (m.member() == "RegisterAgent") {
      agentPath = qvariant_cast<QDBusObjectPath>(m.arguments().first()).path();
      client = m.service();
      agents.append({client, agentPath});
    }
    if (hold && m.member() == "Connect")
      held.append(m);
    else
      bus.send(m.createReply());
    return true;
  }
  void changed() {
    auto s = QDBusMessage::createSignal(device, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    s << deviceInterface << QVariantMap{} << QStringList{"RSSI"};
    bus.send(s);
  }
  QDBusMessage agentCall(const QString& member) {
    auto m = QDBusMessage::createMethodCall(client, agentPath, "org.bluez.Agent1", member);
    return m;
  }
};
class BlueZTest : public testing::Test {
 protected:
  QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "bluetooth-test-service");
  FakeBlueZ fake{bus};
  void SetUp() override {
    qDBusRegisterMetaType<BluetoothObjects>();
    qDBusRegisterMetaType<BluetoothInterfaces>();
    ASSERT_TRUE(bus.registerService(service));
    ASSERT_TRUE(bus.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
  }
  void TearDown() override {
    bus.unregisterObject("/", QDBusConnection::UnregisterTree);
    bus.unregisterService(service);
  }
};
}  // namespace
TEST_F(BlueZTest, GlobalDiscoveryDoesNotImplyOwnership) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto id = c.adapters()->items().first().id;
  QSignalSpy finished(&c, &BluetoothController::operationFinished);
  c.execute(BluetoothOperation::StopDiscovery, id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 1; }));
  EXPECT_EQ(fake.calls.count("StopDiscovery"), 0);
  c.execute(BluetoothOperation::StartDiscovery, id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 2; }));
  c.execute(BluetoothOperation::StartDiscovery, id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 3; }));
  EXPECT_EQ(fake.calls.count("StartDiscovery"), 1);
  c.execute(BluetoothOperation::StopDiscovery, id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 4; }));
  EXPECT_EQ(fake.calls.count("StopDiscovery"), 1);
}
TEST_F(BlueZTest, InvalidationAndRestartRetireIdentityAndPendingWork) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto id = c.devices()->items().first().id;
  fake.objects[QDBusObjectPath(device)][deviceInterface].remove("RSSI");
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !c.devices()->items().first().rssi.has_value(); }));
  fake.hold = true;
  QSignalSpy finished(&c, &BluetoothController::operationFinished);
  c.execute(BluetoothOperation::Connect, id);
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.held.size() == 1; }));
  bus.unregisterService(service);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !c.available(); }));
  ASSERT_EQ(finished.size(), 1);
  ASSERT_TRUE(bus.registerService(service));
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  EXPECT_NE(c.devices()->items().first().id, id);
  bus.send(fake.held.first().createReply());
  QCoreApplication::processEvents();
  EXPECT_EQ(finished.size(), 1);
}
TEST_F(BlueZTest, AgentDisplaysLeadingZerosAndDelaysInputUntilValidResponse) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  QSignalSpy requests(&c, &BluetoothController::agentRequested);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available() && !fake.agentPath.isEmpty(); }));
  auto confirm = fake.agentCall("RequestConfirmation");
  confirm << QVariant::fromValue(QDBusObjectPath(device)) << quint32(42);
  QDBusPendingCallWatcher call(bus.asyncCall(confirm));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 1; }));
  auto r = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
  EXPECT_EQ(r.code, "000042");
  EXPECT_FALSE(call.isFinished());
  c.respond(r.requestId, true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return call.isFinished(); }));
  EXPECT_FALSE(call.isError());
  auto input = fake.agentCall("RequestPasskey");
  input << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher inputCall(bus.asyncCall(input));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 2; }));
  r = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
  c.respond(r.requestId, true, "invalid");
  EXPECT_FALSE(inputCall.isFinished());
  c.respond(r.requestId, true, "000042");
  ASSERT_TRUE(QTest::qWaitFor([&] { return inputCall.isFinished(); }));
  QDBusPendingReply<quint32> reply = inputCall;
  EXPECT_EQ(reply.value(), 42u);
}
TEST_F(BlueZTest, AgentRejectsOtherSenders) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available() && !fake.agentPath.isEmpty(); }));
  auto message = fake.agentCall("RequestAuthorization");
  message << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher call(QDBusConnection::sessionBus().asyncCall(message));
  ASSERT_TRUE(QTest::qWaitFor([&] { return call.isFinished(); }));
  EXPECT_TRUE(call.isError());
}
TEST_F(BlueZTest, AgentSupportsPinDisplayPasskeyDisplayAuthorizationAndCancellation) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  QSignalSpy requests(&c, &BluetoothController::agentRequested), canceled(&c, &BluetoothController::agentCanceled);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available() && !fake.agentPath.isEmpty(); }));
  auto pin = fake.agentCall("RequestPinCode");
  pin << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher pinCall(bus.asyncCall(pin));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 1; }));
  auto r = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
  EXPECT_EQ(r.interaction, BluetoothInteraction::PinEntry);
  c.respond(r.requestId, true, QString(17, '1'));
  EXPECT_FALSE(pinCall.isFinished());
  c.respond(r.requestId, true, "0012");
  ASSERT_TRUE(QTest::qWaitFor([&] { return pinCall.isFinished(); }));
  EXPECT_EQ(QDBusPendingReply<QString>(pinCall).value(), "0012");
  for (const auto& method : {QStringLiteral("DisplayPinCode"), QStringLiteral("DisplayPasskey"),
                             QStringLiteral("RequestAuthorization"), QStringLiteral("AuthorizeService")}) {
    SCOPED_TRACE(method.toStdString());
    const auto before = requests.size();
    auto message = fake.agentCall(method);
    message << QVariant::fromValue(QDBusObjectPath(device));
    if (method == "DisplayPinCode") message << QStringLiteral("0012");
    if (method == "DisplayPasskey") message << quint32(12) << QVariant::fromValue(quint16(2));
    if (method == "AuthorizeService") message << QStringLiteral("00001124-0000-1000-8000-00805f9b34fb");
    QDBusPendingCallWatcher call(bus.asyncCall(message));
    ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == before + 1; }));
    r = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
    if (method == "DisplayPasskey") {
      EXPECT_EQ(r.code, "000012");
      EXPECT_EQ(r.entered, 2);
    }
    if (method == "AuthorizeService") EXPECT_FALSE(r.serviceUuid.isEmpty());
    auto cancel = fake.agentCall("Cancel");
    QDBusPendingCallWatcher cancelCall(bus.asyncCall(cancel));
    ASSERT_TRUE(QTest::qWaitFor([&] { return cancelCall.isFinished() && call.isFinished(); }));
  }
  EXPECT_EQ(canceled.size(), 5);
}
TEST_F(BlueZTest, AgentTimeoutAndDeviceRemovalRejectDelayedCalls) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available() && !fake.agentPath.isEmpty(); }));
  backend.findChild<BlueZAgent*>()->setRequestTimeout(1);
  auto message = fake.agentCall("RequestAuthorization");
  message << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher timeout(bus.asyncCall(message));
  ASSERT_TRUE(QTest::qWaitFor([&] { return timeout.isFinished(); }));
  EXPECT_TRUE(timeout.isError());
  backend.findChild<BlueZAgent*>()->setRequestTimeout(120000);
  QSignalSpy requests(&c, &BluetoothController::agentRequested);
  QDBusPendingCallWatcher removed(bus.asyncCall(message));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 1; }));
  fake.objects.remove(QDBusObjectPath(device));
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return removed.isFinished(); }));
  EXPECT_TRUE(removed.isError());
}

TEST_F(BlueZTest, IndependentProcessesUseTheirOwnRegisteredAgents) {
  QProcess first, second;
  const auto helper = QCoreApplication::applicationDirPath() + QStringLiteral("/bluetooth_agent_client");
  first.start(helper, QStringList{});
  second.start(helper, QStringList{});
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.agents.size() == 2; }));
  EXPECT_NE(fake.agents[0].first, fake.agents[1].first);
  for (const auto& endpoint : fake.agents) {
    auto m = QDBusMessage::createMethodCall(endpoint.first, endpoint.second, "org.bluez.Agent1", "RequestPasskey");
    m << QVariant::fromValue(QDBusObjectPath(device));
    QDBusPendingCallWatcher call(bus.asyncCall(m));
    ASSERT_TRUE(QTest::qWaitFor([&] { return call.isFinished(); }));
    QDBusPendingReply<quint32> reply = call;
    EXPECT_FALSE(reply.isError());
    EXPECT_EQ(reply.value(), 42u);
  }
  first.terminate();
  second.terminate();
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return first.state() == QProcess::NotRunning && second.state() == QProcess::NotRunning; }));
}

TEST_F(BlueZTest, ConcurrentChangesInvalidateInitialSnapshot) {
  fake.holdSnapshots = true;
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.snapshots.size() == 1; }));
  const auto stale = fake.objects;
  fake.objects[QDBusObjectPath(adapter)][adapterInterface]["Alias"] = "New name";
  fake.changed();
  fake.holdSnapshots = false;
  bus.send(fake.snapshots.first().createReply({QVariant::fromValue(stale)}));
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  EXPECT_EQ(c.adapters()->items().first().alias, "New name");
}
TEST_F(BlueZTest, AdapterRemovalRetiresChildRequestsAndReusedDevicePaths) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  const auto deviceId = c.devices()->items().first().id;
  fake.hold = true;
  QSignalSpy finished(&c, &BluetoothController::operationFinished);
  c.execute(BluetoothOperation::Connect, deviceId);
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.held.size() == 1; }));
  auto original = fake.objects.take(QDBusObjectPath(adapter));
  auto signal = QDBusMessage::createSignal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
  signal << QVariant::fromValue(QDBusObjectPath(adapter)) << QStringList{adapterInterface};
  bus.send(signal);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.adapters()->rowCount() == 0 && finished.size() == 1; }));
  EXPECT_EQ(c.devices()->rowCount(), 0);
  fake.objects[QDBusObjectPath(adapter)] = original;
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.devices()->rowCount() == 1; }));
  EXPECT_NE(c.devices()->items().first().id, deviceId);
}
TEST_F(BlueZTest, VisibilityDoesNotEnableAfterTimeoutWriteFailure) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController c(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return c.available(); }));
  fake.failingMember = "Set";
  QSignalSpy finished(&c, &BluetoothController::operationFinished);
  c.execute(BluetoothOperation::Discoverable, c.adapters()->items().first().id, true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 1; }));
  EXPECT_FALSE(qvariant_cast<BluetoothResult>(finished.first().first()).succeeded());
  EXPECT_EQ(fake.calls.count("Set"), 1);
}

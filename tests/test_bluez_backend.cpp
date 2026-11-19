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
  explicit FakeBlueZ(QDBusConnection connection) : bus(std::move(connection)) {
    objects[QDBusObjectPath(adapter)][adapterInterface] = {
        {"Address", "AA"},
        {"Alias", "Computer"},
        {"Powered", true},
        {"Discovering", true},
    };
    objects[QDBusObjectPath(device)][deviceInterface] = {
        {"Adapter", QVariant::fromValue(QDBusObjectPath(adapter))},
        {"Alias", "Keyboard"},
        {"RSSI", -40},
    };
  }
  [[nodiscard]] QString introspect(const QString& /*path*/) const override { return {}; }
  bool handleMessage(const QDBusMessage& messageValue, const QDBusConnection& /*connection*/) override {
    if (messageValue.member() == "GetManagedObjects") {
      if (holdSnapshots) {
        snapshots.append(messageValue);
      } else {
        bus.send(messageValue.createReply({QVariant::fromValue(objects)}));
      }
      return true;
    }
    calls.append(messageValue.member());
    if (messageValue.member() == failingMember) {
      bus.send(messageValue.createErrorReply("org.bluez.Error.Failed", "Test failure"));
      return true;
    }
    if (messageValue.member() == "RegisterAgent") {
      agentPath = qvariant_cast<QDBusObjectPath>(messageValue.arguments().first()).path();
      client = messageValue.service();
      agents.append({client, agentPath});
    }
    if (hold && messageValue.member() == "Connect") {
      held.append(messageValue);
    } else {
      bus.send(messageValue.createReply());
    }
    return true;
  }
  void changed() const {
    auto signalMessage = QDBusMessage::createSignal(device, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signalMessage << deviceInterface << QVariantMap{} << QStringList{"RSSI"};
    bus.send(signalMessage);
  }
  [[nodiscard]] QDBusMessage agentCall(const QString& member) const {
    auto messageValue = QDBusMessage::createMethodCall(client, agentPath, "org.bluez.Agent1", member);
    return messageValue;
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
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available(); }));
  const auto identifier = controller.adapters()->items().first().id;
  QSignalSpy finished(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::StopDiscovery, identifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 1; }));
  EXPECT_EQ(fake.calls.count("StopDiscovery"), 0);
  controller.execute(BluetoothOperation::StartDiscovery, identifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 2; }));
  controller.execute(BluetoothOperation::StartDiscovery, identifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 3; }));
  EXPECT_EQ(fake.calls.count("StartDiscovery"), 1);
  controller.execute(BluetoothOperation::StopDiscovery, identifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 4; }));
  EXPECT_EQ(fake.calls.count("StopDiscovery"), 1);
}
TEST_F(BlueZTest, InvalidationAndRestartRetireIdentityAndPendingWork) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available(); }));
  const auto identifier = controller.devices()->items().first().id;
  fake.objects[QDBusObjectPath(device)][deviceInterface].remove("RSSI");
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.devices()->items().first().rssi.has_value(); }));
  fake.hold = true;
  QSignalSpy finished(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::Connect, identifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.held.size() == 1; }));
  bus.unregisterService(service);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.available(); }));
  ASSERT_EQ(finished.size(), 1);
  ASSERT_TRUE(bus.registerService(service));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available(); }));
  EXPECT_NE(controller.devices()->items().first().id, identifier);
  bus.send(fake.held.first().createReply());
  QCoreApplication::processEvents();
  EXPECT_EQ(finished.size(), 1);
}
TEST_F(BlueZTest, AgentDisplaysLeadingZerosAndDelaysInputUntilValidResponse) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  QSignalSpy requests(&controller, &BluetoothController::agentRequested);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available() && !fake.agentPath.isEmpty(); }));
  auto confirm = fake.agentCall("RequestConfirmation");
  confirm << QVariant::fromValue(QDBusObjectPath(device)) << static_cast<quint32>(42);
  QDBusPendingCallWatcher call(bus.asyncCall(confirm));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 1; }));
  auto requestValue = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
  EXPECT_EQ(requestValue.code, "000042");
  EXPECT_FALSE(call.isFinished());
  controller.respond(requestValue.requestId, true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return call.isFinished(); }));
  EXPECT_FALSE(call.isError());
  auto input = fake.agentCall("RequestPasskey");
  input << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher inputCall(bus.asyncCall(input));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 2; }));
  requestValue = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
  controller.respond(requestValue.requestId, true, "invalid");
  EXPECT_FALSE(inputCall.isFinished());
  controller.respond(requestValue.requestId, true, "000042");
  ASSERT_TRUE(QTest::qWaitFor([&] { return inputCall.isFinished(); }));
  QDBusPendingReply<quint32> reply = inputCall;
  EXPECT_EQ(reply.value(), 42U);
}
TEST_F(BlueZTest, AgentRejectsOtherSenders) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available() && !fake.agentPath.isEmpty(); }));
  auto message = fake.agentCall("RequestAuthorization");
  message << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher call(QDBusConnection::sessionBus().asyncCall(message));
  ASSERT_TRUE(QTest::qWaitFor([&] { return call.isFinished(); }));
  EXPECT_TRUE(call.isError());
}
TEST_F(BlueZTest, AgentSupportsPinDisplayPasskeyDisplayAuthorizationAndCancellation) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  QSignalSpy requests(&controller, &BluetoothController::agentRequested);
  QSignalSpy canceled(&controller, &BluetoothController::agentCanceled);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available() && !fake.agentPath.isEmpty(); }));
  auto pin = fake.agentCall("RequestPinCode");
  pin << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher pinCall(bus.asyncCall(pin));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 1; }));
  auto requestValue = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
  EXPECT_EQ(requestValue.interaction, BluetoothInteraction::PinEntry);
  controller.respond(requestValue.requestId, true, QString(17, '1'));
  EXPECT_FALSE(pinCall.isFinished());
  controller.respond(requestValue.requestId, true, "0012");
  ASSERT_TRUE(QTest::qWaitFor([&] { return pinCall.isFinished(); }));
  EXPECT_EQ(QDBusPendingReply<QString>(pinCall).value(), "0012");
  for (const auto& method : {
           QStringLiteral("DisplayPinCode"),
           QStringLiteral("DisplayPasskey"),
           QStringLiteral("RequestAuthorization"),
           QStringLiteral("AuthorizeService"),
       }) {
    SCOPED_TRACE(method.toStdString());
    const auto before = requests.size();
    auto message = fake.agentCall(method);
    message << QVariant::fromValue(QDBusObjectPath(device));
    if (method == "DisplayPinCode") {
      message << QStringLiteral("0012");
    }
    if (method == "DisplayPasskey") {
      message << static_cast<quint32>(12) << QVariant::fromValue(static_cast<quint16>(2));
    }
    if (method == "AuthorizeService") {
      message << QStringLiteral("00001124-0000-1000-8000-00805f9b34fb");
    }
    QDBusPendingCallWatcher call(bus.asyncCall(message));
    ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == before + 1; }));
    requestValue = qvariant_cast<BluetoothAgentRequest>(requests.last().first());
    if (method == "DisplayPasskey") {
      EXPECT_EQ(requestValue.code, "000012");
      EXPECT_EQ(requestValue.entered, 2);
    }
    if (method == "AuthorizeService") {
      EXPECT_FALSE(requestValue.serviceUuid.isEmpty());
    }
    auto cancel = fake.agentCall("Cancel");
    QDBusPendingCallWatcher cancelCall(bus.asyncCall(cancel));
    ASSERT_TRUE(QTest::qWaitFor([&] { return cancelCall.isFinished() && call.isFinished(); }));
  }
  EXPECT_EQ(canceled.size(), 5);
}
TEST_F(BlueZTest, AgentTimeoutAndDeviceRemovalRejectDelayedCalls) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available() && !fake.agentPath.isEmpty(); }));
  backend.findChild<BlueZAgent*>()->setRequestTimeout(1);
  auto message = fake.agentCall("RequestAuthorization");
  message << QVariant::fromValue(QDBusObjectPath(device));
  QDBusPendingCallWatcher timeout(bus.asyncCall(message));
  ASSERT_TRUE(QTest::qWaitFor([&] { return timeout.isFinished(); }));
  EXPECT_TRUE(timeout.isError());
  backend.findChild<BlueZAgent*>()->setRequestTimeout(120000);
  QSignalSpy requests(&controller, &BluetoothController::agentRequested);
  QDBusPendingCallWatcher removed(bus.asyncCall(message));
  ASSERT_TRUE(QTest::qWaitFor([&] { return requests.size() == 1; }));
  fake.objects.remove(QDBusObjectPath(device));
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return removed.isFinished(); }));
  EXPECT_TRUE(removed.isError());
}

TEST_F(BlueZTest, IndependentProcessesUseTheirOwnRegisteredAgents) {
  QProcess first;
  QProcess second;
  const auto helper = QCoreApplication::applicationDirPath() + QStringLiteral("/bluetooth_agent_client");
  first.start(helper, QStringList{});
  second.start(helper, QStringList{});
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.agents.size() == 2; }));
  EXPECT_NE(fake.agents[0].first, fake.agents[1].first);
  for (const auto& endpoint : fake.agents) {
    auto messageValue =
        QDBusMessage::createMethodCall(endpoint.first, endpoint.second, "org.bluez.Agent1", "RequestPasskey");
    messageValue << QVariant::fromValue(QDBusObjectPath(device));
    QDBusPendingCallWatcher call(bus.asyncCall(messageValue));
    ASSERT_TRUE(QTest::qWaitFor([&] { return call.isFinished(); }));
    QDBusPendingReply<quint32> reply = call;
    EXPECT_FALSE(reply.isError());
    EXPECT_EQ(reply.value(), 42U);
  }
  first.terminate();
  second.terminate();
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return first.state() == QProcess::NotRunning && second.state() == QProcess::NotRunning; }));
}

TEST_F(BlueZTest, ConcurrentChangesInvalidateInitialSnapshot) {
  fake.holdSnapshots = true;
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.snapshots.size() == 1; }));
  const auto stale = fake.objects;
  fake.objects[QDBusObjectPath(adapter)][adapterInterface]["Alias"] = "New name";
  fake.changed();
  fake.holdSnapshots = false;
  bus.send(fake.snapshots.first().createReply({QVariant::fromValue(stale)}));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available(); }));
  EXPECT_EQ(controller.adapters()->items().first().alias, "New name");
}
TEST_F(BlueZTest, AdapterRemovalRetiresChildRequestsAndReusedDevicePaths) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available(); }));
  const auto deviceId = controller.devices()->items().first().id;
  fake.hold = true;
  QSignalSpy finished(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::Connect, deviceId);
  ASSERT_TRUE(QTest::qWaitFor([&] { return fake.held.size() == 1; }));
  auto original = fake.objects.take(QDBusObjectPath(adapter));
  auto signal = QDBusMessage::createSignal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
  signal << QVariant::fromValue(QDBusObjectPath(adapter)) << QStringList{adapterInterface};
  bus.send(signal);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.adapters()->rowCount() == 0 && finished.size() == 1; }));
  EXPECT_EQ(controller.devices()->rowCount(), 0);
  fake.objects[QDBusObjectPath(adapter)] = original;
  fake.changed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.devices()->rowCount() == 1; }));
  EXPECT_NE(controller.devices()->items().first().id, deviceId);
}
TEST_F(BlueZTest, VisibilityDoesNotEnableAfterTimeoutWriteFailure) {
  BlueZBackend backend(QDBusConnection::sessionBus(), service);
  BluetoothController controller(&backend);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.available(); }));
  fake.failingMember = "Set";
  QSignalSpy finished(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::Discoverable, controller.adapters()->items().first().id, true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.size() == 1; }));
  EXPECT_FALSE(qvariant_cast<BluetoothResult>(finished.first().first()).succeeded());
  EXPECT_EQ(fake.calls.count("Set"), 1);
}

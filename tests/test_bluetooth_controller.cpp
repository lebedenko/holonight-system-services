#include "BluetoothController.h"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>
using namespace HoloNight::System;
namespace {
class FakeBluetooth : public BluetoothBackend {
 public:
  using BluetoothBackend::BluetoothBackend;
  QList<BluetoothResult> calls;
  bool stopped = false;
  void start() override {
    emit snapshotChanged({{"adapter", "AA", "Computer", true}}, {{.id = "device", .adapterId = "adapter"}}, true);
  }
  void stop() override { stopped = true; }
  void execute(const QString& request, BluetoothOperation operation, const QString& target, const QVariant&) override {
    calls.append({request, target, operation, {}, {}});
  }
  void respond(const QString&, bool, const QString&) override {}
  void requestDefaultAgent() override {}
};
}  // namespace
TEST(BluetoothController, ConflictsFailAsynchronouslyAndCancelRemainsAvailable) {
  FakeBluetooth backend;
  BluetoothController controller(&backend);
  QSignalSpy results(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::Pair, "device");
  controller.execute(BluetoothOperation::Connect, "device");
  controller.execute(BluetoothOperation::CancelPairing, "device");
  EXPECT_EQ(results.size(), 0);
  ASSERT_TRUE(QTest::qWaitFor([&] { return results.size() == 1 && backend.calls.size() == 2; }));
  EXPECT_TRUE(qvariant_cast<BluetoothResult>(results.first().first()).errorName.endsWith("Busy"));
}
TEST(BluetoothController, RemovalRetiresPendingRequestsAndIgnoresLateSuccess) {
  FakeBluetooth backend;
  BluetoothController controller(&backend);
  QSignalSpy results(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::Pair, "device");
  ASSERT_TRUE(QTest::qWaitFor([&] { return backend.calls.size() == 1; }));
  emit backend.snapshotChanged({}, {}, false);
  ASSERT_EQ(results.size(), 1);
  EXPECT_FALSE(qvariant_cast<BluetoothResult>(results.first().first()).succeeded());
  emit backend.operationFinished(backend.calls.first());
  EXPECT_EQ(results.size(), 1);
  EXPECT_FALSE(controller.busy("device"));
}
TEST(BluetoothController, UnknownTargetsFailWithoutCallingBackend) {
  FakeBluetooth backend;
  BluetoothController controller(&backend);
  QSignalSpy results(&controller, &BluetoothController::operationFinished);
  controller.execute(BluetoothOperation::Power, "gone", true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return results.size() == 1; }));
  EXPECT_TRUE(backend.calls.isEmpty());
}
TEST(BluetoothModels, MissingValuesStayUnknownAndPropertyUpdatesKeepRows) {
  BluetoothDeviceModel model;
  model.apply({{.id = "device", .adapterId = "adapter"}});
  EXPECT_FALSE(model.data(model.index(0), BluetoothDeviceModel::BatteryRole).isValid());
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  model.apply({{.id = "device", .adapterId = "adapter", .battery = 0}});
  EXPECT_EQ(reset.size(), 0);
  EXPECT_EQ(changed.size(), 1);
  EXPECT_EQ(model.data(model.index(0), BluetoothDeviceModel::BatteryRole).toInt(), 0);
}

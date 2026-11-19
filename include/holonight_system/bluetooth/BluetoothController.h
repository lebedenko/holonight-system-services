#pragma once
#include "BluetoothAdapterModel.h"
#include "BluetoothBackend.h"
#include "BluetoothDeviceModel.h"

#include <QHash>
namespace HoloNight::System {
class BluetoothController : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(BluetoothController)
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)
 public:
  explicit BluetoothController(QObject* parent = nullptr);
  // Injected backend must outlive this controller.
  explicit BluetoothController(BluetoothBackend* backend, QObject* parent = nullptr);
  ~BluetoothController() override;
  [[nodiscard]] bool available() const { return available_; }
  BluetoothAdapterModel* adapters() { return &adapters_; }
  BluetoothDeviceModel* devices() { return &devices_; }
  [[nodiscard]] const BluetoothAdapterModel* adapters() const { return &adapters_; }
  [[nodiscard]] const BluetoothDeviceModel* devices() const { return &devices_; }
  [[nodiscard]] bool busy(const QString& targetId) const;
  QString execute(BluetoothOperation operation, const QString& targetId, const QVariant& value = {});
  void respond(const QString& requestId, bool accept, const QString& input = {});
  void requestDefaultAgent();
 signals:
  void availableChanged();
  void operationStateChanged();
  void operationFinished(BluetoothResult result);
  void agentRequested(BluetoothAgentRequest request);
  void agentCanceled(QString requestId);
  void diagnostic(QString errorName, QString errorMessage);

 private:
  void finish(const BluetoothResult& result);
  BluetoothBackend* backend_;
  BluetoothAdapterModel adapters_;
  BluetoothDeviceModel devices_;
  bool available_ = false;
  QHash<QString, BluetoothResult> pending_;
};
}  // namespace HoloNight::System

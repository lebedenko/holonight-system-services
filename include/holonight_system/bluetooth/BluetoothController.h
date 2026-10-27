#pragma once
#include "BluetoothAdapterModel.h"
#include "BluetoothBackend.h"
#include "BluetoothDeviceModel.h"

#include <QHash>
namespace HoloNight::System {
class BluetoothController : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY availableChanged)
 public:
  explicit BluetoothController(QObject* parent = nullptr);
  // Injected backend must outlive this controller.
  explicit BluetoothController(BluetoothBackend* backend, QObject* parent = nullptr);
  ~BluetoothController() override;
  bool available() const { return available_; }
  BluetoothAdapterModel* adapters() { return &adapters_; }
  BluetoothDeviceModel* devices() { return &devices_; }
  const BluetoothAdapterModel* adapters() const { return &adapters_; }
  const BluetoothDeviceModel* devices() const { return &devices_; }
  bool busy(const QString& targetId) const;
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
  void finish(BluetoothResult result);
  BluetoothBackend* backend_;
  BluetoothAdapterModel adapters_;
  BluetoothDeviceModel devices_;
  bool available_ = false;
  QHash<QString, BluetoothResult> pending_;
};
}  // namespace HoloNight::System

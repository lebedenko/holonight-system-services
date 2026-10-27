#pragma once
#include "BluetoothTypes.h"

#include <QObject>
namespace HoloNight::System {
// Targets and replies are scoped to a backend connection lifetime. Completion is asynchronous.
class BluetoothBackend : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void execute(const QString& requestId, BluetoothOperation operation, const QString& targetId,
                       const QVariant& value = {}) = 0;
  virtual void respond(const QString& requestId, bool accept, const QString& input = {}) = 0;
  virtual void requestDefaultAgent() = 0;
 signals:
  void snapshotChanged(QList<BluetoothAdapter> adapters, QList<BluetoothDevice> devices, bool available);
  void operationFinished(BluetoothResult result);
  void agentRequested(BluetoothAgentRequest request);
  void agentCanceled(QString requestId);
  void diagnostic(QString errorName, QString errorMessage);
};
}  // namespace HoloNight::System

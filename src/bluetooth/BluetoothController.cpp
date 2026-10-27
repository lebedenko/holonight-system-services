#include "BluetoothController.h"

#include "BlueZBackend.h"

#include <QTimer>
#include <QUuid>
namespace HoloNight::System {
BluetoothController::BluetoothController(QObject* parent) : BluetoothController(new BlueZBackend, parent) {
  backend_->setParent(this);
}
BluetoothController::BluetoothController(BluetoothBackend* backend, QObject* parent)
    : QObject(parent), backend_(backend) {
  Q_ASSERT(backend_);
  connect(backend_, &BluetoothBackend::snapshotChanged, this,
          [this](const QList<BluetoothAdapter>& adapters, const QList<BluetoothDevice>& devices, bool available) {
            adapters_.apply(available ? adapters : QList<BluetoothAdapter>{});
            devices_.apply(available ? devices : QList<BluetoothDevice>{});
            if (available_ != available) {
              available_ = available;
              emit availableChanged();
            }
            const auto pending = pending_;
            for (auto result : pending) {
              if ((!available || result.operation != BluetoothOperation::Forget) && !adapters_.find(result.targetId) &&
                  !devices_.find(result.targetId)) {
                result.errorName = QStringLiteral("org.holonight.Bluetooth.Disappeared");
                result.errorMessage = QStringLiteral("Bluetooth target is no longer available");
                finish(result);
              }
            }
          });
  connect(backend_, &BluetoothBackend::operationFinished, this, &BluetoothController::finish);
  connect(backend_, &BluetoothBackend::agentRequested, this, &BluetoothController::agentRequested);
  connect(backend_, &BluetoothBackend::agentCanceled, this, &BluetoothController::agentCanceled);
  connect(backend_, &BluetoothBackend::diagnostic, this, &BluetoothController::diagnostic);
  backend_->start();
}
BluetoothController::~BluetoothController() { backend_->stop(); }
bool BluetoothController::busy(const QString& targetId) const {
  for (const auto& request : pending_)
    if (request.targetId == targetId && request.errorName.isEmpty()) return true;
  return false;
}
QString BluetoothController::execute(BluetoothOperation operation, const QString& targetId, const QVariant& value) {
  BluetoothResult result{QUuid::createUuid().toString(QUuid::WithoutBraces), targetId, operation, {}, {}};
  if (!available_ || (!adapters_.find(targetId) && !devices_.find(targetId)))
    result.errorName = QStringLiteral("org.holonight.Bluetooth.Unavailable");
  else if (busy(targetId) && operation != BluetoothOperation::CancelPairing)
    result.errorName = QStringLiteral("org.holonight.Bluetooth.Busy");
  pending_.insert(result.requestId, result);
  emit operationStateChanged();
  QTimer::singleShot(0, this, [this, result, value] {
    if (!pending_.contains(result.requestId)) return;
    if (!result.errorName.isEmpty())
      finish(result);
    else
      backend_->execute(result.requestId, result.operation, result.targetId, value);
  });
  return result.requestId;
}
void BluetoothController::finish(BluetoothResult result) {
  if (!pending_.remove(result.requestId)) return;
  emit operationStateChanged();
  emit operationFinished(result);
}
void BluetoothController::respond(const QString& requestId, bool accept, const QString& input) {
  backend_->respond(requestId, accept, input);
}
void BluetoothController::requestDefaultAgent() { backend_->requestDefaultAgent(); }
}  // namespace HoloNight::System

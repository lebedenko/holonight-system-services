#include "StorageController.h"
#include "UDisks2Backend.h"

#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace HoloNight::System {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString error(const char *name) {
  return QStringLiteral("org.holonight.Storage.") + QLatin1String(name);
}
} // namespace
StorageController::StorageController(QObject *parent)
    : StorageController(new UDisks2Backend, parent) {
  backend_->setParent(this);
}
StorageController::StorageController(StorageBackend *backend, QObject *parent)
    : QObject(parent), backend_(backend) {
  Q_ASSERT(backend_);
  connect(backend_, &StorageBackend::snapshotChanged, this,
          &StorageController::snapshot);
  connect(backend_, &StorageBackend::operationFinished, this,
          [this](const StorageResult &result) {
            QString id;
            for (auto it = requests_.cbegin(); it != requests_.cend(); ++it)
              if (it->stepId == result.requestId) {
                id = it.key();
                break;
              }
            if (id.isEmpty())
              return; // Reply from a retired request/connection.
            auto &request = requests_[id];
            if (!result.succeeded()) {
              finish(id, result.errorName, result.errorMessage);
            } else if (request.finalStep) {
              finish(id, {}, {}, result.mountPath);
            } else {
              request.stepId.clear();
              advance(id);
            }
          });
  backend_->start();
}
StorageController::~StorageController() { backend_->stop(); }
void StorageController::snapshot(QList<StorageDrive> drives,
                                 QList<StorageVolume> volumes, bool available) {
  drives_.apply(available ? drives : QList<StorageDrive>{});
  volumes_.apply(available ? volumes : QList<StorageVolume>{});
  if (available_ != available) {
    available_ = available;
    emit availableChanged();
  }
  // Pending backend calls report their own disappearance errors. Do not infer
  // success from removal.
  if (!available_) {
    const auto ids = requests_.keys();
    for (const auto &id : ids)
      finish(id, error("Unavailable"),
             QStringLiteral("Storage service disconnected"));
  }
}
bool StorageController::busy(const QString &targetId) const {
  for (const auto &request : requests_)
    if (request.scope.contains(targetId))
      return true;
  return false;
}
QStringList StorageController::removalScope(const QString &driveId,
                                            bool powerOff) const {
  const auto drive = drives_.find(driveId);
  if (!drive)
    return {};
  QStringList scope{driveId};
  if (powerOff && !drive->siblingId.isEmpty())
    for (const auto &other : drives_.items())
      if (other.id != driveId && other.siblingId == drive->siblingId)
        scope.append(other.id);
  const auto driveIds = scope;
  for (const auto &volume : volumes_.items())
    if (driveIds.contains(volume.driveId))
      scope.append(volume.id);
  scope.sort();
  return scope;
}
QString StorageController::mount(const QString &id) {
  return begin(StorageOperation::Mount, id);
}
QString StorageController::unmount(const QString &id) {
  return begin(StorageOperation::Unmount, id);
}
QString StorageController::eject(const QString &id) {
  return begin(StorageOperation::Eject, id);
}
QString StorageController::powerOff(const QString &id,
                                    const QStringList &scope) {
  return begin(StorageOperation::PowerOff, id, scope);
}
QString StorageController::begin(StorageOperation operation,
                                 const QString &targetId,
                                 const QStringList &confirmedScope) {
  const QString id = freshId();
  Request request;
  request.result = {id, targetId, operation, {}, {}, {}};
  QString failure;
  if (!available_)
    failure = QStringLiteral("Unavailable");
  const bool removal = operation == StorageOperation::Eject ||
                       operation == StorageOperation::PowerOff;
  if (removal) {
    const auto drive = drives_.find(targetId);
    request.scope =
        removalScope(targetId, operation == StorageOperation::PowerOff);
    if (!drive)
      failure = QStringLiteral("Disappeared");
    else if (!(operation == StorageOperation::Eject ? drive->canEject
                                                    : drive->canPowerOff))
      failure = QStringLiteral("NotSupported");
    if (operation == StorageOperation::PowerOff && drive) {
      auto confirmed = confirmedScope;
      confirmed.sort();
      if (confirmed != request.scope)
        failure = QStringLiteral("ScopeChanged");
    }
    for (const auto &volume : volumes_.items())
      if (request.scope.contains(volume.id) && !volume.mountPoints.isEmpty())
        request.unmounts.append(volume.id);
  } else {
    const auto volume = volumes_.find(targetId);
    request.scope = {targetId};
    if (!volume)
      failure = QStringLiteral("Disappeared");
    else {
      if (!volume->driveId.isEmpty())
        request.scope.append(volume->driveId);
      if (!(operation == StorageOperation::Mount ? volume->canMount
                                                 : volume->canUnmount))
        failure = QStringLiteral("NotSupported");
    }
  }
  for (const auto &target : request.scope)
    if (busy(target))
      failure = QStringLiteral("Busy");
  // Failed requests never reserve another operation's scope.
  if (!failure.isEmpty())
    request.scope.clear();
  requests_.insert(id, request);
  emit operationStateChanged();
  QTimer::singleShot(0, this, [this, id, failure] {
    if (!requests_.contains(id))
      return;
    if (!failure.isEmpty())
      finish(id, QStringLiteral("org.holonight.Storage.") + failure, failure);
    else
      advance(id);
  });
  return id;
}
void StorageController::advance(const QString &id) {
  if (!requests_.contains(id))
    return;
  auto &request = requests_[id];
  const auto operation = request.result.operation;
  const auto target = request.result.targetId;
  if (operation == StorageOperation::Eject ||
      operation == StorageOperation::PowerOff) {
    if (removalScope(target, operation == StorageOperation::PowerOff) !=
        request.scope) {
      finish(id, error("ScopeChanged"),
             QStringLiteral(
                 "Affected storage changed; review the operation again"));
      return;
    }
  } else if (!volumes_.find(target)) {
    finish(id, error("Disappeared"), QStringLiteral("Volume disappeared"));
    return;
  }
  request.stepId = freshId();
  if (!request.unmounts.isEmpty()) {
    const auto volumeId = request.unmounts.takeFirst();
    const auto volume = volumes_.find(volumeId);
    if (!volume || !volume->canUnmount) {
      finish(
          id, error("Disappeared"),
          QStringLiteral("Affected volume is no longer available for unmount"));
      return;
    }
    backend_->execute(request.stepId, StorageOperation::Unmount, volumeId);
  } else {
    if (operation == StorageOperation::Eject ||
        operation == StorageOperation::PowerOff) {
      for (const auto &volume : volumes_.items()) {
        if (request.scope.contains(volume.id) &&
            !volume.mountPoints.isEmpty()) {
          finish(id, error("Busy"),
                 QStringLiteral("An affected volume is still mounted"));
          return;
        }
      }
      const auto drive = drives_.find(target);
      if (!drive ||
          !(operation == StorageOperation::Eject ? drive->canEject
                                                 : drive->canPowerOff)) {
        finish(id, error("NotSupported"),
               QStringLiteral("Drive removal capability changed"));
        return;
      }
    }
    request.finalStep = true;
    backend_->execute(request.stepId, operation, target);
  }
}
void StorageController::finish(const QString &id, const QString &name,
                               const QString &message, const QString &path) {
  if (!requests_.contains(id))
    return;
  auto result = requests_.take(id).result;
  result.errorName = name;
  result.errorMessage = message;
  result.mountPath = path;
  emit operationStateChanged();
  emit operationFinished(result);
}
} // namespace HoloNight::System

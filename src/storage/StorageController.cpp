#include "StorageController.h"

#include "UDisks2Backend.h"

#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace HoloNight::System {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString error(const char* name) { return QStringLiteral("org.holonight.Storage.") + QLatin1String(name); }
}  // namespace
StorageController::StorageController(QObject* parent) : StorageController(new UDisks2Backend, parent) {
  backend_->setParent(this);
}
StorageController::StorageController(StorageBackend* backend, QObject* parent) : QObject(parent), backend_(backend) {
  Q_ASSERT(backend_);
  connect(backend_, &StorageBackend::snapshotChanged, this, &StorageController::snapshot);
  connect(backend_, &StorageBackend::operationFinished, this, [this](const StorageResult& result) {
    QString identifier;
    for (auto it = requests_.cbegin(); it != requests_.cend(); ++it) {
      if (it->step_id == result.requestId) {
        identifier = it.key();
        break;
      }
    }
    if (identifier.isEmpty()) {
      return;  // Reply from a retired request/connection.
    }
    auto& request = requests_[identifier];
    if (!result.succeeded()) {
      finish(identifier, result.errorName, result.errorMessage);
    } else if (request.final_step) {
      finish(identifier, {}, {}, result.mountPath);
    } else {
      request.step_id.clear();
      advance(identifier);
    }
  });
  backend_->start();
}
StorageController::~StorageController() { backend_->stop(); }
void StorageController::snapshot(const QList<StorageDrive>& drives, const QList<StorageVolume>& volumes,
                                 bool available) {
  if (available) {
    drives_.apply(drives);
    volumes_.apply(volumes);
  } else {
    drives_.apply({});
    volumes_.apply({});
  }
  if (available_ != available) {
    available_ = available;
    emit availableChanged();
  }
  // Pending backend calls report their own disappearance errors. Do not infer
  // success from removal.
  if (!available_) {
    const auto ids = requests_.keys();
    for (const auto& identifier : ids) {
      finish(identifier, error("Unavailable"), QStringLiteral("Storage service disconnected"));
    }
  }
}
bool StorageController::busy(const QString& targetId) const {
  return std::ranges::any_of(requests_, [&targetId](const auto& request) { return request.scope.contains(targetId); });
}
QStringList StorageController::removalScope(const QString& driveId, bool powerOff) const {
  const auto drive = drives_.find(driveId);
  if (!drive) {
    return {};
  }
  QStringList scope{driveId};
  if (powerOff && !drive->siblingId.isEmpty()) {
    for (const auto& other : drives_.items()) {
      if (other.id != driveId && other.siblingId == drive->siblingId) {
        scope.append(other.id);
      }
    }
  }
  const auto driveIds = scope;
  for (const auto& volume : volumes_.items()) {
    if (driveIds.contains(volume.driveId)) {
      scope.append(volume.id);
    }
  }
  scope.sort();
  return scope;
}
QString StorageController::mount(const QString& volumeId) { return begin(StorageOperation::Mount, volumeId); }
QString StorageController::unmount(const QString& volumeId) { return begin(StorageOperation::Unmount, volumeId); }
QString StorageController::eject(const QString& driveId) { return begin(StorageOperation::Eject, driveId); }
QString StorageController::powerOff(const QString& driveId, const QStringList& confirmedScope) {
  return begin(StorageOperation::PowerOff, driveId, confirmedScope);
}
// Keep the ordered capability/scope checks beside their transaction transitions.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
QString StorageController::begin(StorageOperation operation, const QString& targetId,
                                 const QStringList& confirmedScope) {
  const QString identifier = freshId();
  Request request;
  request.result = {.requestId = identifier,
                    .targetId = targetId,
                    .operation = operation,
                    .mountPath = {},
                    .errorName = {},
                    .errorMessage = {}};
  QString failure;
  if (!available_) {
    failure = QStringLiteral("Unavailable");
  }
  const bool removal = operation == StorageOperation::Eject || operation == StorageOperation::PowerOff;
  if (removal) {
    const auto drive = drives_.find(targetId);
    request.scope = removalScope(targetId, operation == StorageOperation::PowerOff);
    if (!drive) {
      failure = QStringLiteral("Disappeared");
    } else if (!(operation == StorageOperation::Eject ? drive->canEject : drive->canPowerOff)) {
      failure = QStringLiteral("NotSupported");
    }
    if (operation == StorageOperation::PowerOff && drive) {
      auto confirmed = confirmedScope;
      confirmed.sort();
      if (confirmed != request.scope) {
        failure = QStringLiteral("ScopeChanged");
      }
    }
    for (const auto& volume : volumes_.items()) {
      if (request.scope.contains(volume.id) && !volume.mountPoints.isEmpty()) {
        request.unmounts.append(volume.id);
      }
    }
  } else {
    const auto volume = volumes_.find(targetId);
    request.scope = {targetId};
    if (!volume) {
      failure = QStringLiteral("Disappeared");
    } else {
      if (!volume->driveId.isEmpty()) {
        request.scope.append(volume->driveId);
      }
      if (!(operation == StorageOperation::Mount ? volume->canMount : volume->canUnmount)) {
        failure = QStringLiteral("NotSupported");
      }
    }
  }
  for (const auto& target : request.scope) {
    if (busy(target)) {
      failure = QStringLiteral("Busy");
    }
  }
  // Failed requests never reserve another operation's scope.
  if (!failure.isEmpty()) {
    request.scope.clear();
  }
  requests_.insert(identifier, request);
  emit operationStateChanged();
  QTimer::singleShot(0, this, [this, identifier, failure] {
    if (!requests_.contains(identifier)) {
      return;
    }
    if (!failure.isEmpty()) {
      finish(identifier, QStringLiteral("org.holonight.Storage.") + failure, failure);
    } else {
      advance(identifier);
    }
  });
  return identifier;
}
// Keep the ordered capability/scope checks beside their transaction transitions.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void StorageController::advance(const QString& requestId) {
  if (!requests_.contains(requestId)) {
    return;
  }
  auto& request = requests_[requestId];
  const auto operation = request.result.operation;
  const auto target = request.result.targetId;
  if (operation == StorageOperation::Eject || operation == StorageOperation::PowerOff) {
    if (removalScope(target, operation == StorageOperation::PowerOff) != request.scope) {
      finish(requestId, error("ScopeChanged"), QStringLiteral("Affected storage changed; review the operation again"));
      return;
    }
  } else if (!volumes_.find(target)) {
    finish(requestId, error("Disappeared"), QStringLiteral("Volume disappeared"));
    return;
  }
  request.step_id = freshId();
  if (!request.unmounts.isEmpty()) {
    const auto volumeId = request.unmounts.takeFirst();
    const auto volume = volumes_.find(volumeId);
    if (!volume || !volume->canUnmount) {
      finish(requestId, error("Disappeared"), QStringLiteral("Affected volume is no longer available for unmount"));
      return;
    }
    backend_->execute(request.step_id, StorageOperation::Unmount, volumeId);
  } else {
    if (operation == StorageOperation::Eject || operation == StorageOperation::PowerOff) {
      for (const auto& volume : volumes_.items()) {
        if (request.scope.contains(volume.id) && !volume.mountPoints.isEmpty()) {
          finish(requestId, error("Busy"), QStringLiteral("An affected volume is still mounted"));
          return;
        }
      }
      const auto drive = drives_.find(target);
      if (!drive || !(operation == StorageOperation::Eject ? drive->canEject : drive->canPowerOff)) {
        finish(requestId, error("NotSupported"), QStringLiteral("Drive removal capability changed"));
        return;
      }
    }
    request.final_step = true;
    backend_->execute(request.step_id, operation, target);
  }
}
void StorageController::finish(const QString& requestId, const QString& errorName, const QString& errorMessage,
                               const QString& mountPath) {
  if (!requests_.contains(requestId)) {
    return;
  }
  auto result = requests_.take(requestId).result;
  result.errorName = errorName;
  result.errorMessage = errorMessage;
  result.mountPath = mountPath;
  emit operationStateChanged();
  emit operationFinished(result);
}
}  // namespace HoloNight::System

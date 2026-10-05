#include "LabwcBackend.h"

#include <QGuiApplication>
#include <QRasterWindow>
#include <QTimer>

#include <functional>
#include <print>

namespace {
const CompositorWindow* findWindow(const CompositorSnapshot& snapshot, const QString& title) {
  const CompositorWindow* target = nullptr;
  for (const auto& candidate : snapshot.windows) {
    if (candidate.title == title) {
      target = &candidate;
    }
  }
  return target;
}
bool validateOperations(LabwcBackend& backend, const CompositorWindow& target, int version) {
  if (target.operations.contains(WindowCommand::Fullscreen) != (version >= 2)) {
    QCoreApplication::exit(3);
    return false;
  }
  if (version == 1 &&
      backend.requestWindowCommand(target.id, WindowCommand::Fullscreen) != WindowCommandResult::Unsupported) {
    QCoreApplication::exit(4);
    return false;
  }
  return true;
}
void advanceProtocol(const CompositorSnapshot& snapshot, LabwcBackend& backend, QRasterWindow& window, int version,
                     QString& identifier, int& stage, const std::function<void(WindowCommand)>& request) {
  const auto* target = findWindow(snapshot, window.title());
  if (stage == 0 && (target != nullptr)) {
    identifier = target->id;
    if (!validateOperations(backend, *target, version)) {
      return;
    }
    stage = 1;
    request(WindowCommand::Minimize);
  } else if (stage == 1 && (target != nullptr) && target->minimized) {
    stage = 2;
    request(WindowCommand::Restore);
    request(WindowCommand::Activate);
  } else if (stage == 2 && (target != nullptr) && !target->minimized && target->activated) {
    stage = 3;
    request(WindowCommand::Maximize);
  } else if (stage == 3 && (target != nullptr) && target->maximized) {
    stage = 4;
    request(WindowCommand::Unmaximize);
  } else if (stage == 4 && (target != nullptr) && !target->maximized) {
    if (version >= 2) {
      stage = 5;
      request(WindowCommand::Fullscreen);
    } else {
      stage = 7;
      request(WindowCommand::Close);
    }
  } else if (stage == 5 && (target != nullptr) && target->fullscreen) {
    stage = 6;
    request(WindowCommand::Unfullscreen);
  } else if (stage == 6 && (target != nullptr) && !target->fullscreen) {
    stage = 7;
    request(WindowCommand::Close);
  } else if (stage == 7 && (target == nullptr)) {
    if (backend.requestWindowCommand(identifier, WindowCommand::Activate) != WindowCommandResult::InvalidWindow) {
      QCoreApplication::exit(5);
      return;
    }
    std::println("labwc negotiated protocol v{}: inventory, commands and stale IDs passed", version);
    QCoreApplication::exit(0);
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QGuiApplication::setQuitOnLastWindowClosed(false);
  const int version = QCoreApplication::arguments().value(1).toInt();
  if (version < 1 || version > 3) {
    return 1;
  }
  LabwcBackend backend(nullptr, version);
  QRasterWindow window;
  window.setTitle("labwc-version-smoke");
  window.resize(320, 200);
  QString identifier;
  int stage = 0;
  const auto request = [&](WindowCommand command) {
    if (backend.requestWindowCommand(identifier, command) != WindowCommandResult::Accepted) {
      QCoreApplication::exit(2);
    }
  };
  QObject::connect(&backend, &CompositorBackend::snapshotReady, &app, [&](const CompositorSnapshot& snapshot) {
    advanceProtocol(snapshot, backend, window, version, identifier, stage, request);
  });
  QTimer::singleShot(12000, &app, [&] { QCoreApplication::exit(6); });
  backend.start();
  window.show();
  return QGuiApplication::exec();
}

#include <QCoreApplication>

#include <holonight_system/compositor/CompositorFactory.h>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto backend = createCompositorBackend(QStringLiteral("unsupported"));
  bool reported_unknown = false;
  QObject::connect(
      backend.get(), &CompositorBackend::snapshotReady, [&reported_unknown](const CompositorSnapshot& snapshot) {
        reported_unknown = externalTitleBarForApplication(snapshot, 42, "app") == ExternalTitleBarState::Unknown;
      });
  backend->start();
  return reported_unknown ? 0 : 1;
}

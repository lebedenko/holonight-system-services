#include <QCoreApplication>

#include <holonight_system/compositor/CompositorFactory.h>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto backend = createCompositorBackend(QStringLiteral("unsupported"));
  bool reported_unavailable = false;
  QObject::connect(
      backend.get(), &CompositorBackend::snapshotReady, [&reported_unavailable](const CompositorSnapshot& snapshot) {
        reported_unavailable = !snapshot.connected && snapshot.windows.isEmpty() && !snapshot.diagnostic.isEmpty();
      });
  backend->start();
  return reported_unavailable ? 0 : 1;
}

#include "BlueZBackend.h"
#include "BluetoothController.h"

#include <QCoreApplication>
using namespace HoloNight::System;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  BlueZBackend backend(QDBusConnection::sessionBus(), QStringLiteral("org.holonight.TestBlueZ"));
  BluetoothController controller(&backend);
  QObject::connect(&controller, &BluetoothController::agentRequested, &controller,
                   [&controller](const BluetoothAgentRequest& request) {
                     controller.respond(request.requestId, true, QStringLiteral("000042"));
                   });
  return app.exec();
}

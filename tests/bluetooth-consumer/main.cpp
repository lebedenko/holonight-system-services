#include <QCoreApplication>

#include <BluetoothController.h>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  HoloNight::System::BluetoothController controller;
  return controller.adapters()->rowCount();
}

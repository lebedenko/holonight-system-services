#include <QCoreApplication>
#include <StorageController.h>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  HoloNight::System::StorageController controller;
  return controller.drives()->rowCount();
}

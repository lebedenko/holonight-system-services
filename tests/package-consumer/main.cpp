#include <AudioController.h>

int main() {
  HoloNight::System::AudioController controller(
      HoloNight::System::AudioController::SkipInit);
  return controller.available() ? 1 : 0;
}

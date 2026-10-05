#include "CompositorFactory.h"

#include "HyprlandBackend.h"
#include "SwayBackend.h"
#ifdef HOLONIGHT_COMPOSITOR_WAYLAND
#include "GenericBackend.h"
#include "LabwcBackend.h"
#endif
#include <QSet>

namespace {
class UnavailableBackend final : public CompositorBackend {
 public:
  void start() override { emit snapshotReady({.diagnostic = QStringLiteral("Compositor interface unavailable")}); }
  void activateWorkspace(const QString& /*workspace_id*/) override {}
};
}  // namespace
QString selectCompositorBackend(const QProcessEnvironment& environment) {
  QSet<QString> desktops;
  for (const auto& token : environment.value(QStringLiteral("XDG_CURRENT_DESKTOP")).split(':')) {
    const auto desktop = token.trimmed().toLower();
    if (desktop == "hyprland" || desktop == "sway" || desktop == "labwc") {
      desktops.insert(desktop);
    }
  }
  if (!desktops.isEmpty()) {
    return desktops.size() == 1 ? *desktops.begin() : QStringLiteral("wayland");
  }
  QSet<QString> markers;
  if (!environment.value(QStringLiteral("HYPRLAND_INSTANCE_SIGNATURE")).isEmpty()) {
    markers.insert("hyprland");
  }
  if (!environment.value(QStringLiteral("SWAYSOCK")).isEmpty()) {
    markers.insert("sway");
  }
  if (!environment.value(QStringLiteral("LABWC_PID")).isEmpty()) {
    markers.insert("labwc");
  }
  return markers.size() == 1 ? *markers.begin() : QStringLiteral("wayland");
}
std::unique_ptr<CompositorBackend> createCompositorBackend(const QString& identifier) {
  if (identifier == "hyprland") {
    return std::make_unique<HyprlandBackend>();
  }
  if (identifier == "sway") {
    return std::make_unique<SwayBackend>();
  }
#ifdef HOLONIGHT_COMPOSITOR_WAYLAND
  if (identifier == "labwc") {
    return std::make_unique<LabwcBackend>();
  }
  if (identifier == "wayland") {
    return std::make_unique<GenericBackend>();
  }
#endif
  return std::make_unique<UnavailableBackend>();
}
std::unique_ptr<CompositorBackend> createCompositorBackend() {
  return createCompositorBackend(selectCompositorBackend(QProcessEnvironment::systemEnvironment()));
}
ExternalTitleBarState externalTitleBarForApplication(const CompositorSnapshot& snapshot, quint32 pid,
                                                     const QString& app_id) {
  if (!snapshot.connected || pid == 0 || app_id.isEmpty()) {
    return ExternalTitleBarState::Unknown;
  }
  const CompositorWindow* match = nullptr;
  for (const auto& window : snapshot.windows) {
    if (window.pid != pid || window.app_id != app_id) {
      continue;
    }
    if (match != nullptr) {
      return ExternalTitleBarState::Unknown;
    }
    match = &window;
  }
  return (match != nullptr) ? match->external_title_bar : ExternalTitleBarState::Unknown;
}

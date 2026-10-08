#include "CompositorFactory.h"
#include "SwayIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace {
QJsonObject leaf() {
  return {
      {"type", "con"},
      {"id", 42},
      {"pid", 123},
      {"app_id", "viewer"},
      {"shell", "xdg_shell"},
      {"name", "image.png"},
      {"fullscreen_mode", 0},
      {"nodes", QJsonArray{}},
      {"floating_nodes", QJsonArray{}},
  };
}
std::optional<CompositorSnapshot> inventory(const QJsonObject& window, int fullscreen = 0, QString type = "con") {
  const QJsonObject parent{{"type", type}, {"fullscreen_mode", fullscreen}, {"nodes", QJsonArray{window}}};
  return parseSwaySnapshot("[]", "[]", QJsonDocument(parent).toJson());
}
}  // namespace
TEST(CompositorInventory, NativeIdentityAndTitleDoNotRequireDecorationOrAncestorLayout) {
  const auto snapshot = inventory(leaf());
  ASSERT_TRUE(snapshot);
  ASSERT_EQ(snapshot->windows.size(), 1);
  const auto& window = snapshot->windows.first();
  EXPECT_EQ(window.id, "42");
  EXPECT_EQ(window.pid, 123U);
  EXPECT_EQ(window.app_id, "viewer");
  EXPECT_EQ(window.title, "image.png");
  EXPECT_FALSE(window.fullscreen);
}
TEST(CompositorInventory, InheritsContainerFullscreenButIgnoresWorkspaceCompatibilityMarker) {
  for (const int mode : {1, 2}) {
    const auto container = inventory(leaf(), mode);
    ASSERT_TRUE(container);
    ASSERT_EQ(container->windows.size(), 1);
    EXPECT_TRUE(container->windows.first().fullscreen);
    const auto workspace = inventory(leaf(), mode, "workspace");
    ASSERT_TRUE(workspace);
    ASSERT_EQ(workspace->windows.size(), 1);
    EXPECT_FALSE(workspace->windows.first().fullscreen);
  }
  auto window = leaf();
  window["fullscreen_mode"] = 1;
  EXPECT_TRUE(inventory(window)->windows.first().fullscreen);
}
TEST(CompositorInventory, RejectsInvalidPidAndContainerIdentity) {
  for (const auto* key : {"id", "pid"}) {
    for (const auto& value : {QJsonValue(0), QJsonValue(-1), QJsonValue(1.5), QJsonValue("123")}) {
      auto window = leaf();
      window[key] = value;
      const auto snapshot = inventory(window);
      ASSERT_TRUE(snapshot);
      EXPECT_TRUE(snapshot->windows.isEmpty());
    }
  }
}
TEST(CompositorInventory, SelectionPreservesDesktopPrecedenceAndConservativeFallback) {
  QProcessEnvironment environment;
  environment.insert("SWAYSOCK", "/sway");
  EXPECT_EQ(selectCompositorBackend(environment), "sway");
  environment.insert("XDG_CURRENT_DESKTOP", "Hyprland");
  EXPECT_EQ(selectCompositorBackend(environment), "hyprland");
  environment.insert("XDG_CURRENT_DESKTOP", "Hyprland:Sway");
  EXPECT_EQ(selectCompositorBackend(environment), "wayland");
  environment.remove("XDG_CURRENT_DESKTOP");
  environment.insert("LABWC_PID", "1");
  EXPECT_EQ(selectCompositorBackend(environment), "wayland");
}

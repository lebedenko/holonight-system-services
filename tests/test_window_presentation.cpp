#include "CompositorFactory.h"
#include "SwayIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace {
QJsonObject leaf(QString border = "normal", int height = 22) {
  return {
      {"type", "con"},
      {"id", 42},
      {"pid", 123},
      {"app_id", "org.holonight.Viewer"},
      {"shell", "xdg_shell"},
      {"name", "image.png"},
      {"layout", "none"},
      {"fullscreen_mode", 0},
      {"border", border},
      {"nodes", QJsonArray{}},
      {"floating_nodes", QJsonArray{}},
      {"deco_rect", QJsonObject{{"x", 0}, {"y", 0}, {"width", (height != 0) ? 640 : 0}, {"height", height}}},
  };
}
ExternalTitleBarState observe(const QJsonObject& window, QString layout = "splith", int fullscreen = 0) {
  QJsonObject parent{
      {"type", "con"},
      {"layout", layout},
      {"fullscreen_mode", fullscreen},
      {"deco_rect", QJsonObject{{"x", 0}, {"y", 0}, {"width", 640}, {"height", 22}}},
      {"nodes", QJsonArray{window}},
      {"floating_nodes", QJsonArray{}},
  };
  QJsonObject tree{
      {"type", "root"},
      {"layout", "none"},
      {"fullscreen_mode", 0},
      {"nodes", QJsonArray{parent}},
      {"floating_nodes", QJsonArray{}},
  };
  const auto snapshot = parseSwaySnapshot("[]", "[]", QJsonDocument(tree).toJson());
  EXPECT_TRUE(snapshot.has_value());
  return snapshot ? externalTitleBarForApplication(*snapshot, 123, "org.holonight.Viewer")
                  : ExternalTitleBarState::Unknown;
}
}  // namespace
TEST(WindowPresentation, SwayUsesLeafGeometryRatherThanBorderPolicy) {
  EXPECT_EQ(observe(leaf()), ExternalTitleBarState::Present);
  for (const auto& border : {"pixel", "none", "csd"}) {
    EXPECT_EQ(observe(leaf(border, 0)), ExternalTitleBarState::Absent);
    EXPECT_EQ(observe(leaf(border)), ExternalTitleBarState::Present);
  }
  for (const auto& layout : {"tabbed", "stacked"}) {
    EXPECT_EQ(observe(leaf(), layout), ExternalTitleBarState::Present);
    EXPECT_EQ(observe(leaf("normal", 0), layout), ExternalTitleBarState::Absent);
  }
  EXPECT_EQ(observe(leaf(), "splith", 1), ExternalTitleBarState::Absent);
  EXPECT_EQ(observe(leaf(), "splith", 2), ExternalTitleBarState::Absent);
}
TEST(WindowPresentation, IncompleteAndUnsupportedSwayObservationsStayUnknown) {
  for (const auto& key : {"deco_rect", "fullscreen_mode", "nodes", "app_id", "pid", "layout", "shell"}) {
    auto window = leaf();
    window.remove(key);
    EXPECT_EQ(observe(window), ExternalTitleBarState::Unknown) << key;
  }
  for (const auto& key : {"x", "y", "width", "height"}) {
    auto window = leaf();
    auto rectangle = window["deco_rect"].toObject();
    rectangle[key] = "invalid";
    window["deco_rect"] = rectangle;
    EXPECT_EQ(observe(window), ExternalTitleBarState::Unknown) << key;
  }
  auto window = leaf();
  window["deco_rect"] = QJsonObject{{"x", 0}, {"y", 0}, {"width", -1}, {"height", 22}};
  EXPECT_EQ(observe(window), ExternalTitleBarState::Unknown);
  EXPECT_EQ(observe(leaf(), "unsupported"), ExternalTitleBarState::Unknown);
  // A combined parent decoration never replaces a missing leaf rectangle.
  window = leaf();
  window.remove("deco_rect");
  EXPECT_EQ(observe(window, "tabbed"), ExternalTitleBarState::Unknown);
}
TEST(WindowPresentation, IdentityRequiresExactlyOnePidAndAppIdAndConnectedSnapshot) {
  CompositorSnapshot snapshot{.connected = true};
  snapshot.windows.append({.app_id = "viewer", .pid = 123, .external_title_bar = ExternalTitleBarState::Present});
  EXPECT_EQ(externalTitleBarForApplication(snapshot, 123, "viewer"), ExternalTitleBarState::Present);
  EXPECT_EQ(externalTitleBarForApplication(snapshot, 123, "other"), ExternalTitleBarState::Unknown);
  EXPECT_EQ(externalTitleBarForApplication(snapshot, 124, "viewer"), ExternalTitleBarState::Unknown);
  snapshot.connected = false;
  EXPECT_EQ(externalTitleBarForApplication(snapshot, 123, "viewer"), ExternalTitleBarState::Unknown);
  snapshot.connected = true;
  snapshot.windows.append(snapshot.windows.first());
  EXPECT_EQ(externalTitleBarForApplication(snapshot, 123, "viewer"), ExternalTitleBarState::Unknown);
  snapshot.windows.clear();
  EXPECT_EQ(externalTitleBarForApplication(snapshot, 123, "viewer"), ExternalTitleBarState::Unknown);
}
TEST(WindowPresentation, SelectionPreservesDesktopPrecedenceAndConservativeFallback) {
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

TEST(WindowPresentation, WorkspaceFullscreenCompatibilityMarkerDoesNotHideTitles) {
  auto window = leaf();
  QJsonObject workspace{
      {"type", "workspace"},         {"name", "1"},
      {"layout", "splith"},          {"fullscreen_mode", 1},
      {"nodes", QJsonArray{window}}, {"floating_nodes", QJsonArray{}},
  };
  QJsonObject output{
      {"type", "output"},
      {"name", "DP-1"},
      {"layout", "output"},
      {"fullscreen_mode", 0},
      {"nodes", QJsonArray{workspace}},
      {"floating_nodes", QJsonArray{}},
  };
  QJsonObject tree{
      {"type", "root"},
      {"layout", "none"},
      {"fullscreen_mode", 0},
      {"nodes", QJsonArray{output}},
      {"floating_nodes", QJsonArray{}},
  };
  auto snapshot = parseSwaySnapshot("[]", "[]", QJsonDocument(tree).toJson());
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(externalTitleBarForApplication(*snapshot, 123, "org.holonight.Viewer"), ExternalTitleBarState::Present);
  window["fullscreen_mode"] = 1;
  workspace["nodes"] = QJsonArray{window};
  output["nodes"] = QJsonArray{workspace};
  tree["nodes"] = QJsonArray{output};
  snapshot = parseSwaySnapshot("[]", "[]", QJsonDocument(tree).toJson());
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(externalTitleBarForApplication(*snapshot, 123, "org.holonight.Viewer"), ExternalTitleBarState::Absent);
}

TEST(WindowPresentation, IncompleteAncestorsDoNotEstablishRenderedTitleState) {
  const QJsonObject window = leaf();
  for (const auto& missing : {"layout", "nodes", "floating_nodes", "fullscreen_mode"}) {
    QJsonObject tree{
        {"type", "root"},
        {"layout", "splith"},
        {"fullscreen_mode", 0},
        {"nodes", QJsonArray{window}},
        {"floating_nodes", QJsonArray{}},
    };
    tree.remove(missing);
    const auto snapshot = parseSwaySnapshot("[]", "[]", QJsonDocument(tree).toJson());
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(externalTitleBarForApplication(*snapshot, 123, "org.holonight.Viewer"), ExternalTitleBarState::Unknown);
  }
}

TEST(WindowPresentation, UnsupportedShellAndOutOfRangeGeometryStayUnknown) {
  auto window = leaf();
  window["shell"] = "xwayland";
  EXPECT_EQ(observe(window), ExternalTitleBarState::Unknown);
  for (const auto& number : {2147483648.0, -2147483649.0, 1.5}) {
    window = leaf();
    auto rectangle = window["deco_rect"].toObject();
    rectangle["width"] = number;
    window["deco_rect"] = rectangle;
    EXPECT_EQ(observe(window), ExternalTitleBarState::Unknown);
  }
}

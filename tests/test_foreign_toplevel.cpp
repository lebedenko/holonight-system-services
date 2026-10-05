#include "ForeignToplevelState.h"

#include <gtest/gtest.h>

TEST(LabwcWindows, PropertiesCommitAtomically) {
  ForeignToplevelState state;
  quint64 order = 0;
  state.pending.window = {.app_id = "app", .title = "title"};
  state.setActivated(true, order);
  EXPECT_TRUE(state.committed.window.title.isEmpty());
  EXPECT_EQ(ForeignToplevelState::active({&state}), nullptr);
  state.commit();
  EXPECT_EQ(state.committed.window.title, "title");
  EXPECT_EQ(ForeignToplevelState::active({&state}), &state);
  state.pending.window.title = "new title";
  state.pending.window.app_id = "new app";
  EXPECT_EQ(state.committed.window.app_id, "app");
  state.commit();
  EXPECT_EQ(state.committed.window.title, "new title");
  EXPECT_EQ(state.committed.window.app_id, "new app");
}

TEST(LabwcWindows, FocusTransferInEitherOrderAndClosure) {
  for (bool deactivate_first : {false, true}) {
    ForeignToplevelState first;
    ForeignToplevelState second;
    quint64 order = 0;
    first.setActivated(true, order);
    first.commit();
    if (deactivate_first) {
      first.setActivated(false, order);
      first.commit();
    }
    second.setActivated(true, order);
    second.commit();
    EXPECT_EQ(ForeignToplevelState::active({&first, &second}), &second);
    first.setActivated(false, order);
    first.commit();
    EXPECT_EQ(ForeignToplevelState::active({&first, &second}), &second);
    EXPECT_EQ(ForeignToplevelState::active({&first}), nullptr);
    EXPECT_EQ(ForeignToplevelState::active({}), nullptr);
  }
}

TEST(LabwcWindows, OutputMovementSpanningUnknownAndRemoval) {
  ForeignToplevelState state;
  int first_token{};
  int second_token{};
  int unknown_token{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Opaque identity; never dereferenced.
  auto* first = reinterpret_cast<wl_output*>(&first_token);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Opaque identity; never dereferenced.
  auto* second = reinterpret_cast<wl_output*>(&second_token);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): Opaque identity; never dereferenced.
  auto* unknown = reinterpret_cast<wl_output*>(&unknown_token);
  QHash<wl_output*, QString> names{{first, "DP-1"}, {second, "DP-2"}};
  state.pending.window.title = "title";
  state.pending.outputs = {first};
  state.commit();
  EXPECT_EQ(state.onOutputs(names).size(), 1);
  EXPECT_FALSE(state.onOutputs(names).contains("DP-2"));
  state.pending.outputs.insert(second);
  EXPECT_EQ(state.onOutputs(names).size(), 1);
  state.commit();
  EXPECT_EQ(state.onOutputs(names).size(), 2);
  state.pending.outputs.remove(first);
  state.pending.outputs.insert(unknown);
  state.commit();
  EXPECT_EQ(state.onOutputs(names).size(), 1);
  names.remove(second);
  EXPECT_TRUE(state.onOutputs(names).isEmpty());
}

TEST(LabwcWindows, SourcesMergeIndependentlyAndProtocolAvailabilitySurvivesEmptyWindows) {
  CompositorSnapshot workspace{
      .connected = true,
      .capabilities = {.workspace_listing = true, .workspace_activation = true},
      .workspaces = {{.id = "one"}},
  };
  QHash<QString, CompositorActiveWindow> windows{{"DP-1", {.app_id = "app", .title = "title"}}};
  auto snapshot = mergeLabwcSnapshot(workspace, true, windows);
  EXPECT_TRUE(snapshot.capabilities.active_window);
  EXPECT_EQ(snapshot.workspaces.size(), 1);
  EXPECT_EQ(snapshot.active_windows.value("DP-1").title, "title");
  workspace.workspaces[0].display_name = "renamed";
  snapshot = mergeLabwcSnapshot(workspace, true, windows);
  EXPECT_EQ(snapshot.active_windows.value("DP-1").title, "title");
  EXPECT_EQ(snapshot.workspaces[0].display_name, "renamed");
  windows["DP-1"].title = "changed";
  snapshot = mergeLabwcSnapshot(workspace, true, windows);
  EXPECT_EQ(snapshot.workspaces[0].display_name, "renamed");
  EXPECT_EQ(snapshot.active_windows.value("DP-1").title, "changed");
  snapshot = mergeLabwcSnapshot(workspace, false, windows);
  EXPECT_TRUE(snapshot.connected);
  EXPECT_FALSE(snapshot.capabilities.active_window);
  EXPECT_TRUE(snapshot.active_windows.isEmpty());
  EXPECT_TRUE(snapshot.capabilities.workspace_activation);
  snapshot = mergeLabwcSnapshot(workspace, true, {});
  EXPECT_TRUE(snapshot.capabilities.active_window);
  EXPECT_TRUE(snapshot.active_windows.isEmpty());
  workspace.connected = false;
  snapshot = mergeLabwcSnapshot(workspace, true, windows);
  EXPECT_TRUE(snapshot.connected);
  EXPECT_FALSE(snapshot.capabilities.workspace_listing);
  EXPECT_TRUE(snapshot.workspaces.isEmpty());
  EXPECT_EQ(snapshot.active_windows.size(), 1);
  snapshot = mergeLabwcSnapshot(workspace, false, windows);
  EXPECT_FALSE(snapshot.connected);
  EXPECT_TRUE(snapshot.active_windows.isEmpty());
}

TEST(LabwcWindows, IndependentFlagsAndUncommittedInventory) {
  ForeignToplevelState state;
  EXPECT_FALSE(state.ready);
  state.pending.minimized = true;
  state.pending.maximized = true;
  state.pending.fullscreen = true;
  EXPECT_FALSE(state.committed.minimized);
  state.commit();
  EXPECT_TRUE(state.ready);
  EXPECT_TRUE(state.committed.minimized);
  EXPECT_TRUE(state.committed.maximized);
  EXPECT_TRUE(state.committed.fullscreen);
  EXPECT_FALSE(state.committed.activated);
}

TEST(LabwcWindows, FullscreenRequiresVersionTwo) {
  for (int version : {1, 2, 3}) {
    const auto operations = foreignToplevelOperations(version);
    EXPECT_TRUE(operations.contains(WindowCommand::Activate));
    EXPECT_TRUE(operations.contains(WindowCommand::Close));
    EXPECT_EQ(operations.contains(WindowCommand::Fullscreen), version >= 2);
    EXPECT_EQ(operations.contains(WindowCommand::Unfullscreen), version >= 2);
  }
}

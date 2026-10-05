#include "GenericWorkspaceState.h"

#include <gtest/gtest.h>

TEST(GenericWorkspaceProtocol, DuplicateMutableNamesDoNotIdentifyHandles) {
  GenericWorkspaceState first(1);
  GenericWorkspaceState second(2);
  first.setName("duplicate");
  second.setName("duplicate");
  EXPECT_NE(first.workspace.id, second.workspace.id);
  const auto identifier = first.workspace.id;
  first.setName("renamed");
  EXPECT_EQ(first.workspace.id, identifier);
  first.setId("stable");
  first.setName("another rename");
  EXPECT_EQ(first.workspace.id, "protocol:stable");
  first.setId("");
  EXPECT_EQ(first.workspace.id, "protocol:stable");
}
TEST(GenericWorkspaceProtocol, HiddenStateAndChangingActivationCapabilities) {
  GenericWorkspaceState workspace(1);
  EXPECT_FALSE(workspace.workspace.can_activate);
  workspace.setCapabilities(1);
  EXPECT_TRUE(workspace.workspace.can_activate);
  workspace.setCapabilities(2);
  EXPECT_FALSE(workspace.workspace.can_activate);
  workspace.setState(7);
  EXPECT_TRUE(workspace.hidden);
  EXPECT_TRUE(workspace.workspace.active);
  EXPECT_TRUE(workspace.workspace.urgent);
  workspace.setState(0);
  EXPECT_FALSE(workspace.hidden);
  EXPECT_FALSE(workspace.workspace.active);
  EXPECT_FALSE(workspace.workspace.urgent);
}

#include "CompositorBackend.h"
#include "WindowActivation.h"

#include <gtest/gtest.h>
#include <memory>

namespace {
WindowActivationRequest request(QList<quint32> lineage, QString title = {}) {
  return {.process_lineage = std::move(lineage), .title_hint = std::move(title)};
}
}  // namespace

TEST(WindowActivationResolver, RejectsInvalidAndOverLimitRequests) {
  EXPECT_EQ(resolveWindowActivation(request({}), {}).result, WindowActivationResult::InvalidRequest);
  EXPECT_EQ(resolveWindowActivation(request({42, 0}), {}).result, WindowActivationResult::InvalidRequest);
  EXPECT_EQ(resolveWindowActivation(request(QList<quint32>(65, 42)), {}).result,
            WindowActivationResult::InvalidRequest);
  EXPECT_EQ(resolveWindowActivation(request({42}, QString(513, QLatin1Char('x'))), {}).result,
            WindowActivationResult::InvalidRequest);
  EXPECT_EQ(resolveWindowActivation(request({42}, QString(QChar::Null)), {}).result,
            WindowActivationResult::InvalidRequest);
  EXPECT_TRUE(isValidWindowActivationRequest(request({42}, QString(256, QChar(0x00E9)))));
  EXPECT_FALSE(isValidWindowActivationRequest(request({42}, QString(257, QChar(0x00E9)))));
}

TEST(WindowActivationResolver, SelectsEarliestUniqueLineageCandidate) {
  const auto resolution = resolveWindowActivation(
      request({300, 200, 100}),
      {{.pid = 100, .title = QStringLiteral("ancestor")}, {.pid = 200, .title = QStringLiteral("parent")}});
  EXPECT_EQ(resolution.result, WindowActivationResult::Accepted);
  EXPECT_EQ(resolution.candidate_index, 1);
}

TEST(WindowActivationResolver, IgnoresDuplicateLineageEntriesAndUnrelatedTitles) {
  const auto resolution = resolveWindowActivation(
      request({300, 300, 200}, QStringLiteral("other")),
      {{.pid = 200, .title = QStringLiteral("other")}, {.pid = 300, .title = QStringLiteral("target")}});
  EXPECT_EQ(resolution.result, WindowActivationResult::Accepted);
  EXPECT_EQ(resolution.candidate_index, 1);
}

TEST(WindowActivationResolver, UsesCaseSensitiveExactTitleOnlyForDisambiguation) {
  const QList<WindowActivationCandidate> candidates{
      {.pid = 42, .title = QStringLiteral("Agent")},
      {.pid = 42, .title = QStringLiteral("agent")},
  };
  EXPECT_EQ(resolveWindowActivation(request({42}), candidates).result, WindowActivationResult::Ambiguous);
  EXPECT_EQ(resolveWindowActivation(request({42}, QStringLiteral("Agent")), candidates).candidate_index, 0);
  EXPECT_EQ(resolveWindowActivation(request({42}, QStringLiteral("AGENT")), candidates).result,
            WindowActivationResult::Missing);
}

TEST(WindowActivationResolver, RejectsMissingAndStillAmbiguousTitleMatches) {
  EXPECT_EQ(resolveWindowActivation(request({42}), {{.pid = 7, .title = QStringLiteral("Agent")}}).result,
            WindowActivationResult::Missing);
  EXPECT_EQ(
      resolveWindowActivation(request({42}, QStringLiteral("Agent")), {{.pid = 42, .title = QStringLiteral("Agent")},
                                                                       {.pid = 42, .title = QStringLiteral("Agent")}})
          .result,
      WindowActivationResult::Ambiguous);
}

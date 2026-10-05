#include "WindowActivation.h"

#include <QHash>

#include <algorithm>
#include <limits>

namespace {
bool containsNull(const QString& value) { return value.contains(QChar::Null); }
}  // namespace

bool isValidWindowActivationRequest(const WindowActivationRequest& request) {
  if (request.process_lineage.isEmpty() ||
      request.process_lineage.size() > WindowActivationRequest::kMaximumLineageEntries ||
      request.title_hint.size() > WindowActivationRequest::kMaximumTitleHintBytes ||
      request.title_hint.toUtf8().size() > WindowActivationRequest::kMaximumTitleHintBytes ||
      containsNull(request.title_hint)) {
    return false;
  }
  return std::ranges::all_of(request.process_lineage, [](quint32 pid) { return pid != 0; });
}

WindowActivationResolution resolveWindowActivation(const WindowActivationRequest& request,
                                                   const QList<WindowActivationCandidate>& candidates) {
  if (!isValidWindowActivationRequest(request)) {
    return {.result = WindowActivationResult::InvalidRequest};
  }

  QHash<quint32, qsizetype> lineage_positions;
  lineage_positions.reserve(request.process_lineage.size());
  for (qsizetype index = 0; index < request.process_lineage.size(); ++index) {
    lineage_positions.tryInsert(request.process_lineage.at(index), index);
  }

  qsizetype preferred_position = std::numeric_limits<qsizetype>::max();
  QList<qsizetype> matches;
  for (qsizetype index = 0; index < candidates.size(); ++index) {
    const auto position = lineage_positions.constFind(candidates.at(index).pid);
    if (position == lineage_positions.cend()) {
      continue;
    }
    if (*position < preferred_position) {
      preferred_position = *position;
      matches = {index};
    } else if (*position == preferred_position) {
      matches.append(index);
    }
  }

  if (matches.isEmpty()) {
    return {.result = WindowActivationResult::Missing};
  }
  if (matches.size() == 1) {
    return {.result = WindowActivationResult::Accepted, .candidate_index = matches.constFirst()};
  }
  if (request.title_hint.isEmpty()) {
    return {.result = WindowActivationResult::Ambiguous};
  }

  std::optional<qsizetype> titled_match;
  for (const qsizetype index : matches) {
    if (candidates.at(index).title != request.title_hint) {
      continue;
    }
    if (titled_match.has_value()) {
      return {.result = WindowActivationResult::Ambiguous};
    }
    titled_match = index;
  }
  if (!titled_match.has_value()) {
    return {.result = WindowActivationResult::Missing};
  }
  return {.result = WindowActivationResult::Accepted, .candidate_index = titled_match};
}

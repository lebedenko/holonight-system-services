#pragma once

#include "CompositorSnapshot.h"
#include "NumberedWorkspaceProvider.h"
#include "WindowActivation.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

struct SwayIpcFrame {
  quint32 type{0};
  QByteArray payload;
};

struct SwayWindowInfo {
  WindowActivationCandidate candidate;
  quint64 container_id{0};

  bool operator==(const SwayWindowInfo&) const = default;
};

struct SwayRefreshResult {
  CompositorSnapshot snapshot;
  QList<SwayWindowInfo> windows;
  NumberedWorkspaceState numbered;
  QHash<QString, QString> names;
};

class SwayIpcDecoder {
 public:
  static constexpr quint32 kMaximumPayload = 8U * 1024U * 1024U;
  bool append(const QByteArray& bytes);
  [[nodiscard]] QList<SwayIpcFrame> takeFrames();
  [[nodiscard]] QString error() const { return error_; }

 private:
  QByteArray buffer_;
  QList<SwayIpcFrame> frames_;
  QString error_;
};

[[nodiscard]] QByteArray encodeSwayIpcFrame(quint32 type, const QByteArray& payload);
[[nodiscard]] QString escapeSwayWorkspaceName(const QString& name);
[[nodiscard]] std::optional<SwayRefreshResult> parseSwayRefresh(const QByteArray& workspaces_json,
                                                                const QByteArray& outputs_json,
                                                                const QByteArray& tree_json);
[[nodiscard]] std::optional<CompositorSnapshot> parseSwaySnapshot(const QByteArray& workspaces_json,
                                                                  const QByteArray& outputs_json,
                                                                  const QByteArray& tree_json);

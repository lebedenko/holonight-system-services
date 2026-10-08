#include "SwayIpc.h"

#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr QByteArrayView kMagic{"i3-ipc"};
constexpr qsizetype kHeaderSize = 14;

template <typename Integer>
std::optional<Integer> positiveInteger(const QJsonValue& value) {
  if (!value.isDouble()) {
    return std::nullopt;
  }
  const double number = value.toDouble();
  if (!std::isfinite(number) || number < 1.0 ||
      static_cast<long double>(number) > static_cast<long double>(std::numeric_limits<Integer>::max()) ||
      std::floor(number) != number) {
    return std::nullopt;
  }
  return static_cast<Integer>(number);
}

void observeSwayWindow(const QJsonObject& node, bool fullscreen, QList<CompositorWindow>* windows) {
  const bool native_leaf = node.value(QStringLiteral("nodes")).isArray() &&
                           node.value(QStringLiteral("floating_nodes")).isArray() &&
                           node.value(QStringLiteral("nodes")).toArray().isEmpty() &&
                           node.value(QStringLiteral("floating_nodes")).toArray().isEmpty();
  if (node.value("type").toString() == QLatin1String("con") && native_leaf && node.value("app_id").isString() &&
      node.value("shell").toString() == "xdg_shell") {
    if (const auto pid = positiveInteger<quint32>(node.value("pid"))) {
      if (const auto identifier = positiveInteger<quint64>(node.value("id"))) {
        windows->append({
            .id = QString::number(*identifier),
            .title = node.value("name").toString(),
            .app_id = node.value("app_id").toString(),
            .fullscreen = fullscreen,
            .pid = *pid,
        });
      }
    }
  }
}

void projectSwayWindow(const QJsonObject& node, const QString& output, const QString& workspace, bool project_snapshot,
                       QHash<QString, bool>* occupied, QHash<QString, CompositorActiveWindow>* active_windows) {
  const bool leaf = node.value(QStringLiteral("nodes")).toArray().isEmpty() &&
                    node.value(QStringLiteral("floating_nodes")).toArray().isEmpty();
  if (project_snapshot && leaf && !workspace.isEmpty() && node.value("type").toString() == QLatin1String("con")) {
    occupied->insert(workspace, true);
    if (node.value(QStringLiteral("focused")).toBool(false) && !output.isEmpty()) {
      const QJsonObject properties = node.value(QStringLiteral("window_properties")).toObject();
      QString app_id = node.value(QStringLiteral("app_id")).toString();
      if (app_id.isEmpty()) {
        app_id = properties.value(QStringLiteral("class")).toString();
      }
      active_windows->insert(output, {.app_id = app_id, .title = node.value(QStringLiteral("name")).toString()});
    }
  }
}

void inspectSwayTree(const QJsonObject& node, const QString& output, const QString& workspace, bool project_snapshot,
                     QHash<QString, bool>* occupied, QHash<QString, CompositorActiveWindow>* active_windows,
                     QList<SwayWindowInfo>* activation_windows, QList<CompositorWindow>* windows,
                     bool fullscreen = false) {
  const QString type = node.value(QStringLiteral("type")).toString();
  QString next_output = output;
  QString next_workspace = workspace;
  if (type == QLatin1String("output")) {
    next_output = node.value(QStringLiteral("name")).toString();
  }
  if (type == QLatin1String("workspace")) {
    next_workspace = node.value(QStringLiteral("name")).toString();
    if (next_workspace == QLatin1String("__i3_scratch")) {
      project_snapshot = false;
    }
  }

  const auto fullscreen_mode = node.value(QStringLiteral("fullscreen_mode"));
  const bool valid_fullscreen =
      fullscreen_mode.isDouble() &&
      (fullscreen_mode.toDouble() == 0 || fullscreen_mode.toDouble() == 1 || fullscreen_mode.toDouble() == 2);
  // Workspace fullscreen_mode is always 1 for i3 IPC compatibility. Only container
  // nodes report actual per-window/fullscreen-subtree state.
  fullscreen = fullscreen || (type == QLatin1String("con") && valid_fullscreen && fullscreen_mode.toDouble() != 0);
  observeSwayWindow(node, fullscreen, windows);
  if (type == QLatin1String("con")) {
    const auto pid = positiveInteger<quint32>(node.value(QStringLiteral("pid")));
    const auto container_id = positiveInteger<quint64>(node.value(QStringLiteral("id")));
    if (pid && container_id) {
      activation_windows->append({
          .candidate = {.pid = *pid, .title = node.value(QStringLiteral("name")).toString()},
          .container_id = *container_id,
      });
    }
  }

  projectSwayWindow(node, next_output, next_workspace, project_snapshot, occupied, active_windows);

  for (const QString& child_list : {QStringLiteral("nodes"), QStringLiteral("floating_nodes")}) {
    for (const auto value : node.value(child_list).toArray()) {
      if (value.isObject()) {
        inspectSwayTree(value.toObject(), next_output, next_workspace, project_snapshot, occupied, active_windows,
                        activation_windows, windows, fullscreen);
      }
    }
  }
}
}  // namespace

QByteArray encodeSwayIpcFrame(quint32 type, const QByteArray& payload) {
  QByteArray frame;
  QDataStream stream(&frame, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData(kMagic.data(), static_cast<int>(kMagic.size()));
  const auto length = static_cast<quint32>(payload.size());
  stream << length << type;
  frame.append(payload);
  return frame;
}

bool SwayIpcDecoder::append(const QByteArray& bytes) {
  if (!error_.isEmpty()) {
    return false;
  }
  buffer_.append(bytes);
  while (buffer_.size() >= kHeaderSize) {
    if (QByteArrayView(buffer_).first(6) != kMagic) {
      error_ = QStringLiteral("invalid i3 IPC magic");
      return false;
    }
    QDataStream stream(buffer_.left(kHeaderSize));
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.skipRawData(static_cast<int>(kMagic.size()));
    quint32 length = 0;
    stream >> length;
    if (length > kMaximumPayload) {
      error_ = QStringLiteral("i3 IPC payload exceeds 8 MiB");
      return false;
    }
    if (buffer_.size() < kHeaderSize + static_cast<qsizetype>(length)) {
      break;
    }
    quint32 type = 0;
    stream >> type;
    const auto payload_size = static_cast<qsizetype>(length);
    frames_.append({.type = type, .payload = buffer_.mid(kHeaderSize, payload_size)});
    buffer_.remove(0, kHeaderSize + payload_size);
  }
  return true;
}

QList<SwayIpcFrame> SwayIpcDecoder::takeFrames() { return std::exchange(frames_, {}); }

QString escapeSwayWorkspaceName(const QString& name) {
  QString escaped = name;
  escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
  escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
  return escaped;
}

std::optional<SwayRefreshResult> parseSwayRefresh(const QByteArray& workspaces_json, const QByteArray& outputs_json,
                                                  const QByteArray& tree_json) {
  QJsonParseError error;
  const QJsonDocument workspaces = QJsonDocument::fromJson(workspaces_json, &error);
  if (error.error != QJsonParseError::NoError || !workspaces.isArray()) {
    return std::nullopt;
  }
  const QJsonDocument outputs = QJsonDocument::fromJson(outputs_json, &error);
  if (error.error != QJsonParseError::NoError || !outputs.isArray()) {
    return std::nullopt;
  }
  const QJsonDocument tree = QJsonDocument::fromJson(tree_json, &error);
  if (error.error != QJsonParseError::NoError || !tree.isObject()) {
    return std::nullopt;
  }

  CompositorSnapshot snapshot{
      .connected = true,
      .capabilities =
          {
              .workspace_listing = true,
              .workspace_activation = true,
              .active_window = true,
              .focused_output = true,
              .urgency = true,
              .occupancy = true,
          },
  };
  QHash<QString, bool> occupied;
  QList<SwayWindowInfo> activation_windows;
  inspectSwayTree(tree.object(), {}, {}, true, &occupied, &snapshot.active_windows, &activation_windows,
                  &snapshot.windows);

  for (const auto value : outputs.array()) {
    const QJsonObject output = value.toObject();
    if (output.value(QStringLiteral("focused")).toBool(false)) {
      snapshot.focused_output = output.value(QStringLiteral("name")).toString();
      break;
    }
  }

  int order = 0;
  NumberedWorkspaceState numbered{.eligible = true};
  QSet<int> seen;
  QHash<QString, QString> names;
  for (const auto value : workspaces.array()) {
    const QJsonObject workspace = value.toObject();
    const QString name = workspace.value(QStringLiteral("name")).toString();
    if (name.isEmpty() || name == QLatin1String("__i3_scratch")) {
      continue;
    }
    const int number = workspace.value(QStringLiteral("num")).toInt(-1);
    const QString output = workspace.value(QStringLiteral("output")).toString();
    const bool focused = workspace.value(QStringLiteral("focused")).toBool(false);
    const QString identifier = workspace.contains(QStringLiteral("id"))
                                   ? QString::number(workspace.value(QStringLiteral("id")).toInteger())
                                   : QStringLiteral("name:") + name;
    names.insert(identifier, name);
    if (number > 0) {
      numbered.assignments.insert(identifier, number);
    }
    if (number <= 0 || name != QString::number(number) || seen.contains(number)) {
      numbered.eligible = false;
    }
    seen.insert(number);
    snapshot.workspaces.append({
        .id = identifier,
        .display_name = name,
        .stable_order = order++,
        .outputs = output.isEmpty() ? QStringList{} : QStringList{output},
        .active = workspace.value(QStringLiteral("visible")).toBool(false),
        .focused = focused,
        .urgent = workspace.value(QStringLiteral("urgent")).toBool(false),
        .occupied = occupied.value(name, false),
    });
  }
  return SwayRefreshResult{
      .snapshot = std::move(snapshot),
      .windows = std::move(activation_windows),
      .numbered = std::move(numbered),
      .names = std::move(names),
  };
}

std::optional<CompositorSnapshot> parseSwaySnapshot(const QByteArray& workspaces_json, const QByteArray& outputs_json,
                                                    const QByteArray& tree_json) {
  auto refresh = parseSwayRefresh(workspaces_json, outputs_json, tree_json);
  if (!refresh) {
    return std::nullopt;
  }
  return std::move(refresh->snapshot);
}

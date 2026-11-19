#include "SwayInputObserver.h"

#include "SwayIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

using namespace std::chrono_literals;

namespace {
constexpr quint32 kSubscribe = 2;
constexpr quint32 kGetInputs = 100;
constexpr quint32 kInputEvent = 0x80000015;

QString preferredLayout(const QJsonArray& inputs) {
  QString identifier;
  QString layout;
  qsizetype groups = -1;
  for (const auto input : inputs) {
    const auto keyboard = input.toObject();
    const auto name = keyboard.value("xkb_active_layout_name").toString();
    if (keyboard.value("type").toString() != "keyboard" || name.trimmed().isEmpty()) {
      continue;
    }
    const auto candidate_id = keyboard.value("identifier").toString();
    const auto candidate_groups = keyboard.value("xkb_layout_names").toArray().size();
    if (candidate_groups > groups || (candidate_groups == groups && candidate_id < identifier)) {
      groups = candidate_groups;
      identifier = candidate_id;
      layout = name;
    }
  }
  return layout;
}
}  // namespace

namespace HoloNight::System {

class SwayInputObserver::Connection final : public QObject {
 public:
  explicit Connection(SwayInputObserver& observer) : observer_(observer) {
    retry_.setSingleShot(true);
    deadline_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &Connection::open);
    connect(&deadline_, &QTimer::timeout, this, &Connection::reset);
    connect(&socket_, &QLocalSocket::connected, this,
            [this] { socket_.write(encodeSwayIpcFrame(kSubscribe, R"(["input"])")); });
    connect(&socket_, &QLocalSocket::readyRead, this, &Connection::read);
    connect(&socket_, &QLocalSocket::disconnected, this, &Connection::reset);
    connect(&socket_, &QLocalSocket::errorOccurred, this, &Connection::reset);
    open();
  }

  ~Connection() override {
    socket_.disconnect(this);
    socket_.abort();
  }

 private:
  void open() {
    const QString path = qEnvironmentVariable("SWAYSOCK");
    if (path.isEmpty()) {
      retry_.start(1s);
      return;
    }
    deadline_.start(3s);
    socket_.connectToServer(path);
  }

  void reset() {
    if (retry_.isActive()) {
      return;
    }
    retry_.start(1s);
    deadline_.stop();
    socket_.abort();
    decoder_ = {};
    pending_ = false;
    dirty_ = false;
    setLayout({});
  }

  void setLayout(const QString& name) {
    if (observer_.layout_name_ != name) {
      observer_.layout_name_ = name;
      emit observer_.layoutNameChanged();
    }
  }

  void refresh() {
    if (pending_) {
      dirty_ = true;
      return;
    }
    pending_ = true;
    deadline_.start(3s);
    socket_.write(encodeSwayIpcFrame(kGetInputs, {}));
  }

  bool handle(const SwayIpcFrame& frame) {
    const auto message = QJsonDocument::fromJson(frame.payload);
    switch (frame.type) {
      case kSubscribe:
        if (!message.object().value("success").toBool()) {
          return false;
        }
        refresh();
        break;
      case kGetInputs:
        if (!pending_ || !message.isArray()) {
          return false;
        }
        pending_ = false;
        deadline_.stop();
        if (dirty_) {
          dirty_ = false;
          refresh();
        } else {
          setLayout(preferredLayout(message.array()));
        }
        break;
      case kInputEvent: {
        const auto event = message.object();
        const auto change = event.value("change").toString();
        if (event.value("input").toObject().value("type").toString() == "keyboard" &&
            (change == "xkb_layout" || change == "xkb_keymap" || change == "added" || change == "removed")) {
          refresh();
        }
        break;
      }
    }
    return true;
  }

  void read() {
    if (!decoder_.append(socket_.readAll())) {
      reset();
      return;
    }
    for (const auto& frame : decoder_.takeFrames()) {
      if (!handle(frame)) {
        reset();
        return;
      }
    }
  }

  SwayInputObserver& observer_;
  QLocalSocket socket_;
  QTimer retry_;
  QTimer deadline_;
  SwayIpcDecoder decoder_;
  bool pending_{false};
  bool dirty_{false};
};

SwayInputObserver::SwayInputObserver(QObject* parent) : QObject(parent) {}
SwayInputObserver::~SwayInputObserver() = default;

void SwayInputObserver::start() {
  if (!connection_) {
    connection_ = std::make_unique<Connection>(*this);
  }
}

}  // namespace HoloNight::System

#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace HoloNight::System {

class SwayInputObserver final : public QObject {
  Q_OBJECT

 public:
  explicit SwayInputObserver(QObject* parent = nullptr);
  ~SwayInputObserver() override;

  void start();
  [[nodiscard]] QString layoutName() const { return layout_name_; }

 Q_SIGNALS:
  void layoutNameChanged();

 private:
  class Connection;
  QString layout_name_;
  std::unique_ptr<Connection> connection_;
};

}  // namespace HoloNight::System

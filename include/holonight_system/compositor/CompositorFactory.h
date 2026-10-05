#pragma once
#include "CompositorBackend.h"

#include <QProcessEnvironment>

#include <memory>

// Desktop tokens take precedence over markers; conflicting evidence uses the generic backend.
QString selectCompositorBackend(const QProcessEnvironment& environment);
std::unique_ptr<CompositorBackend> createCompositorBackend(const QString& identifier);
std::unique_ptr<CompositorBackend> createCompositorBackend();
ExternalTitleBarState externalTitleBarForApplication(const CompositorSnapshot& snapshot, quint32 pid,
                                                     const QString& app_id);

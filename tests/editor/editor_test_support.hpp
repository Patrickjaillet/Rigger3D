// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
// Outils communs aux tests d'interface (Qt Test, plateforme « offscreen »).
#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

#include "core/character/character.hpp"
#include "io/gltf_import.hpp"

namespace rigger::testing {

inline QString dataFile(const char* name) {
    return QStringLiteral(RIGGER_TEST_DATA_DIR "/") + QString::fromLatin1(name);
}

inline std::shared_ptr<rigger::Character> loadCharacter(const char* name) {
    auto result = rigger::io::importGltf(std::filesystem::path(dataFile(name).toStdWString()));
    if (!result) {
        return nullptr;
    }
    return std::make_shared<rigger::Character>(std::move(result.value()));
}

// Chaque fichier de test enregistre sa classe ici ; test_main.cpp les exécute toutes dans UNE QApplication.
class TestRegistry {
public:
    using Factory = std::function<QObject*()>;
    static std::vector<Factory>& factories() {
        static std::vector<Factory> list;
        return list;
    }
};

template <class T>
struct TestRegistrar {
    TestRegistrar() {
        TestRegistry::factories().push_back([] { return new T; });
    }
};

}  // namespace rigger::testing

#define RIGGER_REGISTER_QTEST(Class) static rigger::testing::TestRegistrar<Class> Class##_registrar

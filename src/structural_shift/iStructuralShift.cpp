#include "structural_shift/iStructuralShift.h"
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/runtime/RuntimeCommand.h>
#include <ll/api/command/runtime/RuntimeOverload.h>
#include <ll/api/event/EventBus.h>
#include <ll/api/event/command/ServerCommandRegisterEvent.h>
#include <ll/api/event/server/ServerStartedEvent.h>
#include <ll/api/event/server/ServerStoppingEvent.h>
#include <ll/api/i18n/I18n.h>
#include <ll/api/io/FileUtils.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/mod/RegisterHelper.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/service/GamingStatus.h>
#include <ll/api/utils/ErrorUtils.h>
#include <ll/api/utils/StringUtils.h>
#include <mc/deps/nbt/CompoundTagVariant.h>
#include <mc/server/commands/CommandOrigin.h>
#include <mc/server/commands/CommandOutput.h>
#include <mc/server/commands/CommandOutputParameter.h>
#include <mc/server/commands/standard/StructureCommand.h>
#include <mc/world/level/BlockPos.h>
#include <mc/world/level/Level.h>
#include <mc/world/level/dimension/DimensionHeightRange.h>
#include <mc/world/level/levelgen/structure/StructureManager.h>
#include <mc/world/level/levelgen/structure/StructureTemplate.h>
#include <mc/world/level/storage/DBStorage.h>


namespace mif::structural_shift {

using namespace ll::i18n_literals;
using namespace fmt::literals;

iStructuralShift& iStructuralShift::getInstance() {
    static iStructuralShift instance;
    return instance;
}

bool iStructuralShift::load() {
    if (auto result = ll::i18n::getInstance().load(getSelf().getLangDir()); !result) {
        getSelf().getLogger().error("Failed to load i18n.");
        result.error().log(getSelf().getLogger());
    }
    auto& eventBus = ll::event::EventBus::getInstance();
    mListeners.insert(eventBus.emplaceListener<ll::event::ServerCommandRegisterEvent>([&](auto&) {
        updateCommandEnum();
        registerCommand();
    }));
    mListeners.insert(eventBus.emplaceListener<ll::event::ServerStoppingEvent>([&](auto&) {
        ll::memory::HookRegistrar<SaveStructureHook, DeleteStructureHook, GetOrCreateStructureHook>::unhook();
    }));
    mListeners.insert(eventBus.emplaceListener<ll::event::ServerStartedEvent>([&](auto&) {
        ll::memory::HookRegistrar<SaveStructureHook, DeleteStructureHook, GetOrCreateStructureHook>::hook();
    }));
    return true;
}

bool iStructuralShift::enable() { return true; }

bool iStructuralShift::disable() { return true; }

bool iStructuralShift::unload() {
    auto& eventBus = ll::event::EventBus::getInstance();
    for (auto& listener : mListeners) {
        eventBus.removeListener(listener);
    }
    if (ll::getGamingStatus() == ll::GamingStatus::Running) {
        ll::memory::HookRegistrar<SaveStructureHook, DeleteStructureHook, GetOrCreateStructureHook>::unhook();
    }
    return true;
}

void iStructuralShift::registerCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
                    .getOrCreateCommand("structural_shift", {}, CommandPermissionLevel::GameDirectors)
                    .alias("iss");

    cmd.runtimeOverload()
        .text("update_enum")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, auto&&) {
        updateCommandEnum();
        output.success("Updated command enum."_trl(origin.getLocaleCode()));
    });
    cmd.runtimeOverload().text("list").execute([](CommandOrigin const& origin, CommandOutput& output, auto&&) {
        auto values = getAllStructuralNames(*origin.getLevel());

        if (values.empty()) {
            output.error("No structures found."_trl(origin.getLocaleCode()));
            return;
        }

        output.success(
            "Found {count} structures: {names}"_trl(
                origin.getLocaleCode(),
                "count"_a = values.size(),
                "names"_a = fmt::join(values, ", ")
            )
        );
    });
    cmd.runtimeOverload()
        .text("export_all")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, auto&&) {
        auto                     names = getAllStructuralNames(*origin.getLevel());
        std::vector<std::string> values;
        ll::Expected<>           errors;

        for (auto const& name : names) {
            if (auto res = exportStructural(name); !res) {
                appendError(
                    errors,
                    ll::makeI18nStringError<"Failed to export {name} structure">("name"_a = name).error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(name);
            }
        }

        if (values.empty() && names.empty()) {
            output.error("No structures found."_trl(origin.getLocaleCode()));
            return;
        }
        if (!values.empty()) {
            output.success(
                "Exported {count} structures: {names}"_trl(
                    origin.getLocaleCode(),
                    "count"_a = values.size(),
                    "names"_a = fmt::join(values, ", ")
                )
            );
            updateCommandEnum();
        }
        if (!errors) {
            errors.error().log(output, origin.getLocaleCode());
        }
    });
    cmd.runtimeOverload()
        .text("export")
        .required("name", ll::command::ParamKind::SoftEnum, "structural_shift_export")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        auto& name = self["name"].get<ll::command::ParamKind::SoftEnum>();
        if (auto res = exportStructural(name); !res) {
            output.error("Failed to export {name} structure"_trl(origin.getLocaleCode(), "name"_a = name));
            res.error().log(output, origin.getLocaleCode());
            return;
        }

        output.success("Exported {name} structure"_trl(origin.getLocaleCode(), "name"_a = name));
        updateCommandEnum();
    });
    cmd.runtimeOverload()
        .text("import_all")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, auto&&) {
        std::vector<std::string> values;
        ll::Expected<>           errors;

        for (auto& entry : std::filesystem::directory_iterator(getSelf().getDataDir() / u8"structures")) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mcstructure") continue;

            if (auto res = importStructural(entry.path()); !res) {
                auto name = ll::string_utils::u8str2str(entry.path().stem().u8string());
                appendError(
                    errors,
                    ll::makeI18nStringError<"Failed to import {name} structure">("name"_a = name).error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(ll::string_utils::u8str2str(entry.path().stem().u8string()));
            }
        }

        if (values.empty() && errors) {
            output.error("No structures found."_trl(origin.getLocaleCode()));
            return;
        }
        if (!values.empty()) {
            output.success(
                "Imported {count} structures: {names}"_trl(
                    origin.getLocaleCode(),
                    "count"_a = values.size(),
                    "names"_a = fmt::join(values, ", ")
                )
            );
            updateCommandEnum();
        }
        if (!errors) {
            errors.error().log(output, origin.getLocaleCode());
        }
    });
    cmd.runtimeOverload()
        .text("import")
        .required("name", ll::command::ParamKind::SoftEnum, "structural_shift_import")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        auto& name = self["name"].get<ll::command::ParamKind::SoftEnum>();
        if (auto res = importStructural((getSelf().getDataDir() / u8"structures" / ll::file_utils::u8path(name))
                                            .replace_extension(".mcstructure"));
            !res) {
            output.error("Failed to import {name} structure"_trl(origin.getLocaleCode(), "name"_a = name));
            res.error().log(output, origin.getLocaleCode());
            return;
        }
        output.success("Imported {name} structure"_trl(origin.getLocaleCode(), "name"_a = name));
        updateCommandEnum();
    });
    cmd.runtimeOverload()
        .text("remove")
        .required("name", ll::command::ParamKind::SoftEnum, "structural_shift_export")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        auto& name = self["name"].get<ll::command::ParamKind::SoftEnum>();
        if (auto res = removeStructural(name); !res) {
            output.error("Failed to remove {name} structure"_trl(origin.getLocaleCode(), "name"_a = name));
            res.error().log(output, origin.getLocaleCode());
            return;
        }

        output.success("Removed {name} structure"_trl(origin.getLocaleCode(), "name"_a = name));
        updateCommandEnum();
    });
    cmd.runtimeOverload()
        .text("remove_all")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, auto&&) {
        auto                     names = getAllStructuralNames(*origin.getLevel());
        std::vector<std::string> values;
        ll::Expected<>           errors;

        for (auto const& name : names) {
            if (auto res = removeStructural(name); !res) {
                appendError(
                    errors,
                    ll::makeI18nStringError<"Failed to remove {name} structure">("name"_a = name).error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(name);
            }
        }

        if (values.empty() && names.empty()) {
            output.error("No structures found."_trl(origin.getLocaleCode()));
            return;
        }
        if (!values.empty()) {
            output.success(
                "Removed {count} structures: {names}"_trl(
                    origin.getLocaleCode(),
                    "count"_a = values.size(),
                    "names"_a = fmt::join(values, ", ")
                )
            );
            updateCommandEnum();
        }
        if (!errors) {
            errors.error().log(output, origin.getLocaleCode());
        }
    });
    cmd.runtimeOverload()
        .text("bypass_limit")
        .optional("status", ll::command::ParamKind::Bool)
        .execute([](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        using Hooks = ll::memory::HookRegistrar<NoStructureRangeLimitHook>;
        static bool mCurrentStatus{false};
        auto const  localeCode = origin.getLocaleCode();

        if (self["status"].has_value()) {
            auto status = self["status"].get<ll::command::ParamKind::Bool>();
            if (status != mCurrentStatus) {
                mCurrentStatus = status;
                status ? Hooks::hook() : Hooks::unhook();
                output.success(
                    "Vanilla structure range limit removal {status}"_trl(
                        localeCode,
                        "status"_a   = status ? "enabled"_trl(localeCode) : "disabled"_trl(localeCode),
                        "enabled"_a  = "enabled"_trl(localeCode),
                        "disabled"_a = "disabled"_trl(localeCode)
                    )
                );
                return;
            }
            output.error(
                "Vanilla structure range limit removal already {status}"_trl(
                    localeCode,
                    "status"_a   = status ? "enabled"_trl(localeCode) : "disabled"_trl(localeCode),
                    "enabled"_a  = "enabled"_trl(localeCode),
                    "disabled"_a = "disabled"_trl(localeCode)
                )
            );
            return;
        }

        output.success(
            "Vanilla structure range limit removal is currently {status}"_trl(
                localeCode,
                "status"_a   = mCurrentStatus ? "enabled"_trl(localeCode) : "disabled"_trl(localeCode),
                "enabled"_a  = "enabled"_trl(localeCode),
                "disabled"_a = "disabled"_trl(localeCode)
            )
        );
    });
}

std::filesystem::path iStructuralShift::getStructuralPath(std::string_view name) {
    return (getSelf().getDataDir() / u8"structures" / ll::file_utils::u8path(name)).replace_extension(".mcstructure");
}

ll::Expected<> iStructuralShift::importStructural(std::filesystem::path const& path) try {
    if (!std::filesystem::exists(path)) return ll::makeI18nStringError<"File not found">();
    if (!std::filesystem::is_regular_file(path)) return ll::makeI18nStringError<"Not a regular file">();
    if (path.extension() != ".mcstructure") return ll::makeI18nStringError<"Not a mcstructure file">();

    auto level = ll::service::getLevel();
    if (!level) return ll::makeI18nStringError<"No level">();
    auto  name    = ll::string_utils::u8str2str(path.stem().u8string());
    auto& stroage = static_cast<DBStorage&>(level->getLevelStorage());

    auto content = ll::file_utils::readFile(path, true);
    if (!content) return ll::makeI18nStringError<"Failed to read file">();

    {
        auto tag = CompoundTag::fromBinaryNbt(*content);
        if (!tag) return ll::forwardError(tag.error());

        auto structure = std::make_unique<StructureTemplate>("", level->getUnknownBlockTypeRegistry());
        if (!structure->load(*tag)) return ll::makeI18nStringError<"Invalid structure template">();
    }

    stroage.saveData(
        fmt::format("structuretemplate_mystructure:{0}", name),
        std::move(*content),
        DBHelpers::Category::All
    );
    level->getStructureManager()->mStructureRepository->erase(fmt::format("mystructure:{0}", name));
    return {};
} catch (...) {
    return ll::makeExceptionError();
}

ll::Expected<> iStructuralShift::exportStructural(std::string_view name) try {
    auto level = ll::service::getLevel();
    if (!level) return ll::makeI18nStringError<"No level">();
    auto& structures = *level->getStructureManager()->mStructureRepository;
    auto& storage    = static_cast<DBStorage&>(level->getLevelStorage());

    auto cacheName   = fmt::format("mystructure:{0}", name);
    auto storageName = fmt::format("structuretemplate_{0}", cacheName);

    if (auto it = structures.find(cacheName); it != structures.end()) {
        auto tag = it->second->save();
        if (!tag) return ll::makeI18nStringError<"Failed to save structure template">();

        auto content = tag->toBinaryNbt();
        if (!ll::file_utils::writeFile(getStructuralPath(name), content, true)) {
            return ll::makeI18nStringError<"Failed to write file">();
        }

        return {};
    }

    if (!storage.hasKey(storageName, DBHelpers::Category::All)) {
        return ll::makeI18nStringError<"Structure template not found">();
    }

    std::string content;
    if (!storage.loadData(storageName, content, DBHelpers::Category::All)) {
        return ll::makeI18nStringError<"Failed to load data">();
    }

    if (!ll::file_utils::writeFile(getStructuralPath(name), content, true)) {
        return ll::makeI18nStringError<"Failed to write file">();
    }

    return {};
} catch (...) {
    return ll::makeExceptionError();
}

ll::Expected<> iStructuralShift::removeStructural(std::string_view name) try {
    auto level = ll::service::getLevel();
    if (!level) return ll::makeI18nStringError<"No level">();
    auto& storage    = static_cast<DBStorage&>(level->getLevelStorage());
    auto& structures = *level->getStructureManager()->mStructureRepository;

    auto cacheName   = fmt::format("mystructure:{0}", name);
    auto storageName = fmt::format("structuretemplate_{0}", cacheName);

    auto fileRemoved{false};
    if (auto it = structures.find(cacheName); it != structures.end()) {
        structures.erase(it);
        fileRemoved = true;
    }

    if (!storage.hasKey(storageName, DBHelpers::Category::All) && !fileRemoved) {
        return ll::makeI18nStringError<"Structure template not found">();
    }

    storage.deleteData(storageName, DBHelpers::Category::All);
    return {};
} catch (...) {
    return ll::makeExceptionError();
}

void iStructuralShift::updateCommandEnum() try {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& registrar = ll::command::CommandRegistrar::getInstance(false);
    for (auto enumName : {"structural_shift_export", "structural_shift_import"}) {
        if (!registrar.hasSoftEnum(enumName)) registrar.tryRegisterSoftEnum(enumName, {});
    }

    registrar.setSoftEnumValues(
        "structural_shift_export",
        getAllStructuralNames(*level) | std::ranges::to<std::vector>()
    );
    {
        auto directory = getSelf().getDataDir() / u8"structures";
        {
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
        }

        std::vector<std::string> values;
        for (auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mcstructure") continue;
            values.emplace_back(ll::string_utils::u8str2str(entry.path().stem().u8string()));
        }
        registrar.setSoftEnumValues("structural_shift_import", std::move(values));
    }
} catch (...) {
    getSelf().getLogger().error("Failed to update command enum");
    ll::error_utils::printCurrentException(getSelf().getLogger());
}

ll::SmallDenseSet<std::string> iStructuralShift::getAllStructuralNames(Level& level) {
    auto&                          stroage = static_cast<DBStorage&>(level.getLevelStorage());
    ll::SmallDenseSet<std::string> values;
    stroage.forEachKeyWithPrefix(
        "structuretemplate_mystructure:",
        DBHelpers::Category::All,
        [&values](std::string_view key, std::string_view) { values.insert(std::string{key}); }
    );
    for (auto& [name, _structure] : *level.getStructureManager()->mStructureRepository) {
        if (!name.starts_with("mystructure:")) continue;
        values.insert(name.substr(sizeof("mystructure:") - 1));
    }
    return values;
}

LL_TYPE_INSTANCE_HOOK(
    iStructuralShift::NoStructureRangeLimitHook,
    HookPriority::Normal,
    StructureCommand,
    &StructureCommand::_isValidSize,
    bool,
    BlockPos const&             size,
    DimensionHeightRange const& heightRange,
    CommandOutput&              output
) {
    if (size.x < 1 || size.y < 1 || size.z < 1) {
        std::vector<CommandOutputParameter> params{
            CommandOutputParameter{std::to_string(1).c_str()},
            CommandOutputParameter{std::to_string(1).c_str()},
            CommandOutputParameter{std::to_string(1).c_str()},
            CommandOutputParameter{std::to_string(size.x).c_str()},
            CommandOutputParameter{std::to_string(size.y).c_str()},
            CommandOutputParameter{std::to_string(size.z).c_str()}
        };
        output.addMessage("commands.structure.size.tooSmall", params, CommandOutputMessageType::Error);
        return false;
    }

    if (auto maxHeight = heightRange.mMax - heightRange.mMin; size.y > maxHeight) {
        std::vector<CommandOutputParameter> params{
            CommandOutputParameter{"∞"},
            CommandOutputParameter{std::to_string(maxHeight).c_str()},
            CommandOutputParameter{"∞"},
            CommandOutputParameter{std::to_string(size.x).c_str()},
            CommandOutputParameter{std::to_string(size.y).c_str()},
            CommandOutputParameter{std::to_string(size.z).c_str()}
        };
        output.addMessage("commands.structure.size.tooBig", params, CommandOutputMessageType::Error);
        return false;
    }

    return true;
}

LL_TYPE_INSTANCE_HOOK(
    iStructuralShift::SaveStructureHook,
    HookPriority::Normal,
    StructureManager,
    &StructureManager::saveToLevel,
    void,
    StructureTemplate const& structureTemplate,
    LevelStorage&            levelStorage
) {
    origin(structureTemplate, levelStorage);
    getInstance().updateCommandEnum();
}

LL_TYPE_INSTANCE_HOOK(
    iStructuralShift::GetOrCreateStructureHook,
    HookPriority::Normal,
    StructureManager,
    &StructureManager::$getOrCreate,
    StructureTemplate&,
    std::string const& structureName
) {
    auto& result = origin(structureName);
    getInstance().updateCommandEnum();
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    iStructuralShift::DeleteStructureHook,
    HookPriority::Normal,
    StructureManager,
    &StructureManager::deleteStructure,
    StructureDeleteResult,
    std::string const& structureName,
    LevelStorage&      levelStorage
) {
    auto result = origin(structureName, levelStorage);
    if (result == StructureDeleteResult::Success) getInstance().updateCommandEnum();
    return result;
}

LL_REGISTER_MOD(iStructuralShift, iStructuralShift::getInstance());

} // namespace mif::structural_shift

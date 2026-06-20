#include "structural_shift/iStructuralShift.h"
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/runtime/RuntimeCommand.h>
#include <ll/api/command/runtime/RuntimeOverload.h>
#include <ll/api/io/FileUtils.h>
#include <ll/api/memory/Hook.h>
#include <ll/api/mod/RegisterHelper.h>
#include <ll/api/service/Bedrock.h>
#include <ll/api/utils/ErrorUtils.h>
#include <ll/api/utils/StringUtils.h>
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
#if __has_include(<mc/nbt/CompoundTagVariant.h>)
#  include <mc/nbt/CompoundTagVariant.h>
#endif
#if __has_include(<mc/deps/nbt/CompoundTagVariant.h>)
#  include <mc/deps/nbt/CompoundTagVariant.h>
#endif

namespace mif::structural_shift {

iStructuralShift& iStructuralShift::getInstance() {
    static iStructuralShift instance;
    return instance;
}

bool iStructuralShift::load() { return true; }

bool iStructuralShift::enable() {
    updateCommandEnum();
    ll::memory::HookRegistrar<SaveStructureHook, DeleteStructureHook>::hook();

    auto& cmd = getCommandRegistrar()
                    .getOrCreateCommand("i_structural_shift", {}, CommandPermissionLevel::GameDirectors)
                    .alias("iss");

    cmd.runtimeOverload()
        .text("update_enum")
        .execute([this](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const&) {
        updateCommandEnum();
        output.success("Updated command enum.");
    });
    cmd.runtimeOverload().text("list").execute(
        [](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const&) {
        auto&                    stroage = static_cast<DBStorage&>(origin.getLevel()->getLevelStorage());
        std::vector<std::string> values;
        stroage.forEachKeyWithPrefix(
            "structuretemplate_mystructure:",
            DBHelpers::Category::All,
            [&values](std::string_view key, std::string_view) { values.emplace_back(key); }
        );

        if (values.empty()) {
            output.error("No structures found.");
            return;
        }

        output.success("Found {0} structures: {1}", values.size(), fmt::join(values, ", "));
    }
    );
    cmd.runtimeOverload()
        .text("export_all")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const&) {
        auto&                    stroage = static_cast<DBStorage&>(origin.getLevel()->getLevelStorage());
        std::vector<std::string> values;
        ll::Expected<>           errors;
        stroage.forEachKeyWithPrefix(
            "structuretemplate_mystructure:",
            DBHelpers::Category::All,
            [this, &values, &errors](std::string_view key, std::string_view) {
            if (auto res = exportStructural(key); !res) {
                appendError(
                    errors,
                    ll::makeStringError(fmt::format("Failed to export {0} structure", key)).error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(key);
            }
        }
        );

        if (values.empty() && errors) {
            output.error("No structures found.");
            return;
        }
        if (!values.empty()) {
            output.success("Exported {0} structures: {1}", values.size(), fmt::join(values, ", "));
            updateCommandEnum();
        }
        if (!errors) {
            errors.error().log(output);
        }
    });
    cmd.runtimeOverload()
        .text("export")
        .required("name", ll::command::ParamKind::SoftEnum, "structural_shift_export")
        .execute([this](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        auto& name = self["name"].get<ll::command::ParamKind::SoftEnum>();
        if (auto res = exportStructural(name); !res) {
            output.error("Failed to export {0} structure", name);
            res.error().log(output);
            return;
        }

        output.success("Exported {0} structure", name);
        updateCommandEnum();
    });
    cmd.runtimeOverload()
        .text("import_all")
        .execute([this](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const&) {
        std::vector<std::string> values;
        ll::Expected<>           errors;

        for (auto& entry : std::filesystem::directory_iterator(getSelf().getDataDir() / u8"structures")) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mcstructure") continue;

            if (auto res = importStructural(entry.path()); !res) {
                appendError(
                    errors,
                    ll::makeStringError(
                        fmt::format(
                            "Failed to import {0} structure",
                            ll::string_utils::u8str2str(entry.path().stem().u8string())
                        )
                    )
                        .error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(ll::string_utils::u8str2str(entry.path().stem().u8string()));
            }
        }

        if (values.empty() && errors) {
            output.error("No structures found.");
            return;
        }
        if (!values.empty()) {
            output.success("Imported {0} structures: {1}", values.size(), fmt::join(values, ", "));
            updateCommandEnum();
        }
        if (!errors) {
            errors.error().log(output);
        }
    });
    cmd.runtimeOverload()
        .text("import")
        .required("name", ll::command::ParamKind::SoftEnum, "structural_shift_import")
        .execute([this](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        auto& name = self["name"].get<ll::command::ParamKind::SoftEnum>();
        if (auto res = importStructural((getSelf().getDataDir() / u8"structures" / ll::file_utils::u8path(name))
                                            .replace_extension(".mcstructure"));
            !res) {
            output.error("Failed to import {0} structure", name);
            res.error().log(output);
            return;
        }
        output.success("Imported {0} structure", name);
        updateCommandEnum();
    });
    cmd.runtimeOverload()
        .text("remove")
        .required("name", ll::command::ParamKind::SoftEnum, "structural_shift_export")
        .execute([this](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        auto& name = self["name"].get<ll::command::ParamKind::SoftEnum>();
        if (auto res = removeStructural(name); !res) {
            output.error("Failed to remove {0} structure", name);
            res.error().log(output);
            return;
        }

        output.success("Removed {0} structure", name);
        updateCommandEnum();
    });
    cmd.runtimeOverload()
        .text("remove_all")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const&) {
        auto&                    stroage = static_cast<DBStorage&>(origin.getLevel()->getLevelStorage());
        std::vector<std::string> names;
        std::vector<std::string> values;
        ll::Expected<>           errors;

        stroage.forEachKeyWithPrefix(
            "structuretemplate_mystructure:",
            DBHelpers::Category::All,
            [&names](std::string_view key, std::string_view) { names.emplace_back(key); }
        );

        for (auto const& name : names) {
            if (auto res = removeStructural(name); !res) {
                appendError(
                    errors,
                    ll::makeStringError(fmt::format("Failed to remove {0} structure", name)).error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(name);
            }
        }

        if (values.empty() && names.empty()) {
            output.error("No structures found.");
            return;
        }
        if (!values.empty()) {
            output.success("Removed {0} structures: {1}", values.size(), fmt::join(values, ", "));
            updateCommandEnum();
        }
        if (!errors) {
            errors.error().log(output);
        }
    });
    cmd.runtimeOverload()
        .text("bypass_limit")
        .optional("status", ll::command::ParamKind::Bool)
        .execute([](CommandOrigin const&, CommandOutput& output, ll::command::RuntimeCommand const& self) {
        using Hooks = ll::memory::HookRegistrar<NoStructureRangeLimitHook>;
        static bool mCurrentStatus{false};

        if (self["status"].has_value()) {
            auto status = self["status"].get<ll::command::ParamKind::Bool>();
            if (status != mCurrentStatus) {
                mCurrentStatus = status;
                status ? Hooks::hook() : Hooks::unhook();
                output.success("Vanilla structure range limit removal {0}", status ? "enabled" : "disabled");
                return;
            }
            output.error("Vanilla structure range limit removal already {0}", status ? "enabled" : "disabled");
            return;
        }

        output.success(
            "Vanilla structure range limit removal is currently {0}",
            mCurrentStatus ? "enabled" : "disabled"
        );
    });
    return true;
}

bool iStructuralShift::disable() {
    ll::memory::HookRegistrar<SaveStructureHook, DeleteStructureHook>::unhook();
    return true;
}

bool iStructuralShift::unload() { return true; }

ll::Expected<> iStructuralShift::importStructural(std::filesystem::path const& path) try {
    if (!std::filesystem::exists(path)) return ll::makeStringError("File not found");
    if (!std::filesystem::is_regular_file(path)) return ll::makeStringError("Not a regular file");
    if (path.extension() != ".mcstructure") return ll::makeStringError("Not a mcstructure file");

    auto level = ll::service::getLevel();
    if (!level) return ll::makeStringError("No level");
    auto& stroage = static_cast<DBStorage&>(level->getLevelStorage());

    auto content = ll::file_utils::readFile(path, true);
    if (!content) return ll::makeStringError("Failed to read file");

    {
        auto tag = CompoundTag::fromBinaryNbt(*content);
        if (!tag) return ll::forwardError(tag.error());

        auto structure = std::make_unique<StructureTemplate>("", level->getUnknownBlockTypeRegistry());
        if (!structure->load(*tag)) return ll::makeStringError("Invalid structure template");
    }

    stroage.saveData(
        fmt::format("structuretemplate_mystructure:{0}", ll::string_utils::u8str2str(path.stem().u8string())),
        std::move(*content),
        DBHelpers::Category::All
    );
    return {};
} catch (...) {
    return ll::makeExceptionError();
}

ll::Expected<> iStructuralShift::exportStructural(std::string_view name) try {
    auto level = ll::service::getLevel();
    if (!level) return ll::makeStringError("No level");
    auto& storage = static_cast<DBStorage&>(level->getLevelStorage());

    auto key = fmt::format("structuretemplate_mystructure:{0}", name);
    if (!storage.hasKey(key, DBHelpers::Category::All)) return ll::makeStringError("Structure template not found");

    std::string content;
    if (!storage.loadData(key, content, DBHelpers::Category::All)) return ll::makeStringError("Failed to load data");

    if (!ll::file_utils::writeFile(
            (getSelf().getDataDir() / u8"structures" / ll::file_utils::u8path(name)).replace_extension(".mcstructure"),
            content,
            true
        )) {
        return ll::makeStringError("Failed to write file");
    }

    return {};
} catch (...) {
    return ll::makeExceptionError();
}

ll::Expected<> iStructuralShift::removeStructural(std::string_view name) try {
    auto level = ll::service::getLevel();
    if (!level) return ll::makeStringError("No level");
    auto& storage = static_cast<DBStorage&>(level->getLevelStorage());

    auto key = fmt::format("structuretemplate_mystructure:{0}", name);
    if (!storage.hasKey(key, DBHelpers::Category::All)) return ll::makeStringError("Structure template not found");

    storage.deleteData(key, DBHelpers::Category::All);
    return {};
} catch (...) {
    return ll::makeExceptionError();
}

void iStructuralShift::updateCommandEnum() try {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& stroage   = static_cast<DBStorage&>(level->getLevelStorage());
    auto& registrar = getCommandRegistrar();
    for (auto enumName : {"structural_shift_export", "structural_shift_import"}) {
        if (!registrar.hasSoftEnum(enumName)) registrar.tryRegisterSoftEnum(enumName, {});
    }

    {
        std::vector<std::string> values;
        stroage.forEachKeyWithPrefix(
            "structuretemplate_mystructure:",
            DBHelpers::Category::All,
            [&values](std::string_view key, std::string_view) { values.emplace_back(key); }
        );
        registrar.setSoftEnumValues("structural_shift_export", std::move(values));
    }
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

ll::command::CommandRegistrar& iStructuralShift::getCommandRegistrar() {
    // clang-format off
    return ll::Overloaded{
        [](auto& (*func)()) -> auto& { return func(); },
        [](auto& (*func)(bool)) -> auto& { return func(false); }
    }(&ll::command::CommandRegistrar::getInstance);
    // clang-format on
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
            CommandOutputParameter("∞"),
            CommandOutputParameter(std::to_string(maxHeight).c_str()),
            CommandOutputParameter("∞"),
            CommandOutputParameter(std::to_string(size.x).c_str()),
            CommandOutputParameter(std::to_string(size.y).c_str()),
            CommandOutputParameter(std::to_string(size.z).c_str())
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

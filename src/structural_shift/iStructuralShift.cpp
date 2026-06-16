#include "structural_shift/iStructuralShift.h"
#include "ll/api/Expected.h"
#include "ll/api/command/runtime/ParamKind.h"
#include "mc/nbt/CompoundTag.h"
#include <ll/api/command/CommandHandle.h>
#include <ll/api/command/CommandRegistrar.h>
#include <ll/api/command/runtime/RuntimeCommand.h>
#include <ll/api/command/runtime/RuntimeOverload.h>
#include <ll/api/io/FileUtils.h>
#include <ll/api/mod/RegisterHelper.h>
#include <ll/api/service/Bedrock.h>
#include <mc/nbt/CompoundTagVariant.h>
#include <mc/world/level/Level.h>
#include <mc/world/level/levelgen/structure/StructureTemplate.h>
#include <mc/world/level/storage/DBStorage.h>


namespace mif::structural_shift {

iStructuralShift& iStructuralShift::getInstance() {
    static iStructuralShift instance;
    return instance;
}

bool iStructuralShift::load() { return true; }

bool iStructuralShift::enable() {
    updateCommandEnum();

    auto& cmd = ll::command::CommandRegistrar::getInstance()
                    .getOrCreateCommand("i_structural_shift", {}, CommandPermissionLevel::GameDirectors)
                    .alias("iss");

    cmd.runtimeOverload()
        .text("export_all")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, ll::command::RuntimeCommand const& self) {
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
                    ll::makeStringError(fmt::format("Failed to import {0} structure", entry.path().filename().string()))
                        .error(),
                    std::move(res.error())
                );
            } else {
                values.emplace_back(entry.path().filename().string());
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
        if (auto res = importStructural(getSelf().getDataDir() / u8"structures" / (name + ".mcstructure")); !res) {
            output.error("Failed to import {0} structure", name);
            res.error().log(output);
            return;
        }
        output.success("Imported {0} structure", name);
        updateCommandEnum();
    });
    return true;
}

bool iStructuralShift::disable() { return true; }

bool iStructuralShift::unload() { return true; }

ll::Expected<> iStructuralShift::importStructural(std::filesystem::path const& path) {
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
        fmt::format("structuretemplate_mystructure:{0}", path.stem().string()),
        std::move(*content),
        DBHelpers::Category::All
    );
    return {};
}

ll::Expected<> iStructuralShift::exportStructural(std::string_view name) {
    auto level = ll::service::getLevel();
    if (!level) return ll::makeStringError("No level");
    auto& stroage = static_cast<DBStorage&>(level->getLevelStorage());

    auto key = fmt::format("structuretemplate_mystructure:{0}", name);
    if (!stroage.hasKey(key, DBHelpers::Category::All)) return ll::makeStringError("Structure template not found");

    std::string content;
    if (!stroage.loadData(key, content, DBHelpers::Category::All)) return ll::makeStringError("Failed to load data");

    if (!ll::file_utils::writeFile(
            getSelf().getDataDir() / u8"structures" / (std::string{name} + ".mcstructure"),
            content,
            true
        )) {
        return ll::makeStringError("Failed to write file");
    }

    return {};
}

void iStructuralShift::updateCommandEnum() {
    auto level = ll::service::getLevel();
    if (!level) return;
    auto& stroage   = static_cast<DBStorage&>(level->getLevelStorage());
    auto& registrar = ll::command::CommandRegistrar::getInstance();
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
        // std::filesystem::
        auto directory = getSelf().getDataDir() / u8"structures";
        {
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
        }

        std::vector<std::string> values;
        for (auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mcstructure") continue;
            values.emplace_back(entry.path().stem().string());
        }
        registrar.setSoftEnumValues("structural_shift_import", std::move(values));
    }
}

LL_REGISTER_MOD(iStructuralShift, iStructuralShift::getInstance());

} // namespace mif::structural_shift
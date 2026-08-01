#pragma once
#include <ll/api/mod/NativeMod.h>

namespace ll::command {
class CommandRegistrar;
}
class Level;

namespace mif::structural_shift {

class iStructuralShift {
public:
    struct NoStructureRangeLimitHook;
    struct GetOrCreateStructureHook;
    struct SaveStructureHook;
    struct DeleteStructureHook;

public:
    static iStructuralShift& getInstance();

    iStructuralShift() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

    std::filesystem::path getStructuralPath(std::string_view name);
    ll::Expected<>        importStructural(std::filesystem::path const& path);
    ll::Expected<>        exportStructural(std::string_view name);
    ll::Expected<>        removeStructural(std::string_view name);
    void                  updateCommandEnum();

    static ll::command::CommandRegistrar& getCommandRegistrar();
    static ll::SmallDenseSet<std::string> getAllStructuralNames(Level& level);

    template <typename First, typename... Rest>
    static ll::Error combineErrors(First&& first, Rest&&... rest) {
        auto combined = std::forward<First>(first);
        (combined.join(std::forward<Rest>(rest)), ...);
        return combined;
    }

    template <typename... Errors>
    static void appendError(ll::Expected<>& target, Errors&&... errs) {
        if constexpr (sizeof...(errs) == 0) return;

        auto combined = combineErrors(std::forward<Errors>(errs)...);
        target        = ll::forwardError(target ? combined : target.error().join(std::move(combined)));
    }

private:
    ll::mod::NativeMod& mSelf;
};

} // namespace mif::structural_shift

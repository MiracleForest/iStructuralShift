#pragma once
#include "ll/api/Expected.h"
#include "ll/api/coro/Generator.h"
#include <ll/api/mod/NativeMod.h>

namespace mif::structural_shift {

class iStructuralShift {
public:
    static iStructuralShift& getInstance();

    iStructuralShift() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();
    bool unload();

    ll::Expected<> importStructural(std::filesystem::path const& path);
    ll::Expected<> exportStructural(std::string_view name);
    void           updateCommandEnum();

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
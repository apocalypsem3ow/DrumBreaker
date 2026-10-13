#pragma once

#include <atomic>
#include <memory>

namespace drumbreaker::licensing
{

class LicenseClient;

class LicenseAccess
{
public:
    LicenseAccess() = default;
    bool isVerified() const noexcept
    {
        return authorization != nullptr && authorization->load();
    }

private:
    friend class LicenseClient;
    explicit LicenseAccess (std::shared_ptr<const std::atomic<bool>> state) : authorization (std::move (state)) {}
    std::shared_ptr<const std::atomic<bool>> authorization;
};

} // namespace drumbreaker::licensing
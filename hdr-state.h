#pragma once

enum class HdrState { Unavailable, Inactive, Active };

constexpr HdrState hdrStateFromCount(unsigned hdrCount) {
    return hdrCount ? HdrState::Active : HdrState::Inactive;
}

constexpr Mode automaticTarget(Mode desired, HdrState hdr) {
    if (hdr == HdrState::Unavailable) return Mode::Unknown;
    return hdr == HdrState::Active ? desired : Mode::Srgb;
}

constexpr bool canSelectMode(Mode mode, HdrState hdr, Mode current, bool busy) {
    return index(mode) < selectableCount && !busy && current != Mode::Unknown &&
        (mode == Mode::Srgb || hdr == HdrState::Active);
}

inline HdrState detectHdr() noexcept {
    try {
        constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
        UINT32 count = 0, pathModeCount = 0;
        if (GetDisplayConfigBufferSizes(flags, &count, &pathModeCount)) return HdrState::Unavailable;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(pathModeCount);
        if (QueryDisplayConfig(flags, &count, paths.data(), &pathModeCount, modes.data(), nullptr))
            return HdrState::Unavailable;
        unsigned hdrCount = 0;
        for (UINT32 i = 0; i < count; ++i) {
            const auto& target = paths[i].targetInfo;
            DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 info{};
            info.header = {DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2,
                           sizeof(info), target.adapterId, target.id};
            if (DisplayConfigGetDeviceInfo(&info.header)) return HdrState::Unavailable;
            if (info.activeColorMode == DISPLAYCONFIG_ADVANCED_COLOR_MODE_HDR) ++hdrCount;
        }
        // 只看已开启的 HDR，不把 HDR 能力或 SDR 自动色彩管理当作 HDR。
        return hdrStateFromCount(hdrCount);
    } catch (...) {
        return HdrState::Unavailable;
    }
}

/*
 * Fooyin
 * Copyright 2022-2023, Luke Taylor <LukeT1@proton.me>
 *
 * Fooyin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fooyin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fooyin.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#include "fygui_export.h"

#include <utils/settings/settingsentry.h>

#include <QObject>

namespace Fooyin {
class SettingsManager;
class LibraryTreeGroupRegistry;
class PresetRegistry;
class PlaylistColumnRegistry;

namespace Settings::Gui {
Q_NAMESPACE_EXPORT(FYGUI_EXPORT)
enum GuiSettings : uint32_t
{
    LayoutEditing         = 1 | Type::Bool,
    StartupBehaviour      = 2 | Type::Int,
    WaitForTracks         = 3 | Type::Bool,
    IconTheme             = 4 | Type::Int,
    LastPlaylistId        = 5 | Type::Int,
    CursorFollowsPlayback = 6 | Type::Bool,
    PlaybackFollowsCursor = 7 | Type::Bool,
};
Q_ENUM_NS(GuiSettings)
} // namespace Settings::Gui

class FYGUI_EXPORT GuiSettings
{
public:
    explicit GuiSettings(SettingsManager* settingsManager);
    ~GuiSettings();

    void shutdown();

    [[nodiscard]] LibraryTreeGroupRegistry* libraryTreeGroupRegistry() const;
    [[nodiscard]] PresetRegistry* playlistPresetRegistry() const;
    [[nodiscard]] PlaylistColumnRegistry* playlistColumnRegistry() const;

private:
    SettingsManager* m_settings;
    std::unique_ptr<LibraryTreeGroupRegistry> m_libraryTreeGroupRegistry;
    std::unique_ptr<PresetRegistry> m_playlistPresetRegistry;
    std::unique_ptr<PlaylistColumnRegistry> m_playlistColumnRegistry;
};
} // namespace Fooyin

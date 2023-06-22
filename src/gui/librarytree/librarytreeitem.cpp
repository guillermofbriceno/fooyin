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

#include "librarytreeitem.h"

#include <QVariant>

namespace Fy::Gui::Widgets {

LibraryTreeItem::LibraryTreeItem(QString title, LibraryTreeItem* parent)
    : TreeItem{parent}
    , m_pending{true}
    , m_title{std::move(title)}
{ }

bool LibraryTreeItem::pending() const
{
    return m_pending;
}

void LibraryTreeItem::setPending(bool pending)
{
    m_pending = pending;
}

QString LibraryTreeItem::title() const
{
    return m_title;
}

void LibraryTreeItem::setTitle(const QString& title)
{
    m_title = title;
}

Core::TrackList LibraryTreeItem::tracks() const
{
    return m_tracks;
}

int LibraryTreeItem::trackCount() const
{
    return static_cast<int>(m_tracks.size());
}

void LibraryTreeItem::addTrack(const Core::Track& track)
{
    m_tracks.emplace_back(track);
}

} // namespace Fy::Gui::Widgets
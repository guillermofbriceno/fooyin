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

#include "playlisttabs.h"

#include "gui/widgetfactory.h"

#include <core/playlist/playlisthandler.h>

#include <utils/actions/actioncontainer.h>
#include <utils/actions/actionmanager.h>

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QTabBar>

namespace Fy::Gui::Widgets {
PlaylistTabs::PlaylistTabs(Utils::ActionManager* actionManager, WidgetFactory* widgetFactory,
                           Core::Playlist::PlaylistHandler* playlistHandler, QWidget* parent)
    : FyWidget{parent}
    , m_actionManager{actionManager}
    , m_widgetFactory{widgetFactory}
    , m_playlistHandler{playlistHandler}
    , m_layout{new QVBoxLayout(this)}
    , m_tabs{new QTabBar(this)}
{
    setObjectName(PlaylistTabs::name());

    m_layout->setContentsMargins(0, 0, 0, 0);

    m_tabs->setMovable(false);
    m_tabs->setExpanding(false);

    m_layout->addWidget(m_tabs);

    QObject::connect(m_tabs, &QTabBar::currentChanged, this, &PlaylistTabs::tabChanged);
    QObject::connect(m_playlistHandler,
                     &Core::Playlist::PlaylistHandler::currentPlaylistChanged,
                     this,
                     &PlaylistTabs::playlistChanged);
    QObject::connect(
        m_playlistHandler, &Core::Playlist::PlaylistHandler::playlistAdded, this, &PlaylistTabs::addPlaylist);
    QObject::connect(
        m_playlistHandler, &Core::Playlist::PlaylistHandler::playlistRemoved, this, &PlaylistTabs::removePlaylist);
    QObject::connect(
        m_playlistHandler, &Core::Playlist::PlaylistHandler::playlistRenamed, this, &PlaylistTabs::playlistRenamed);

    setupTabs();
}

QString PlaylistTabs::name() const
{
    return "Playlist Tabs";
}

QString PlaylistTabs::layoutName() const
{
    return "PlaylistTabs";
}

void PlaylistTabs::setupTabs()
{
    const auto& playlists = m_playlistHandler->playlists();
    for(const auto& playlist : playlists) {
        addPlaylist(playlist.get());
    }
}

int PlaylistTabs::addPlaylist(Core::Playlist::Playlist* playlist)
{
    const int index = addNewTab(playlist->name());
    if(index >= 0) {
        m_tabs->setTabData(index, playlist->id());
        m_tabs->setCurrentIndex(index);
        show();
    }
    return index;
}

void PlaylistTabs::removePlaylist(int id)
{
    for(int i = 0; i < m_tabs->count(); ++i) {
        if(m_tabs->tabData(i).toInt() == id) {
            m_tabs->removeTab(i);
        }
    }
}

int PlaylistTabs::addNewTab(const QString& name, const QIcon& icon)
{
    if(name.isEmpty()) {
        return -1;
    }
    return m_tabs->addTab(icon, name);
}

void PlaylistTabs::contextMenuEvent(QContextMenuEvent* event)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    auto* createPlaylist = new QAction("Add New Playlist", menu);
    QObject::connect(
        createPlaylist, &QAction::triggered, m_playlistHandler, &Core::Playlist::PlaylistHandler::createEmptyPlaylist);
    menu->addAction(createPlaylist);

    const QPoint point = event->pos();
    const int index    = m_tabs->tabAt(point);
    if(index >= 0) {
        const int id = m_tabs->tabData(index).toInt();

        auto* renamePlaylist = new QAction("Rename Playlist", menu);
        QObject::connect(renamePlaylist, &QAction::triggered, this, [this, index, id]() {
            bool success       = false;
            const QString text = QInputDialog::getText(
                this, tr("Rename Playlist"), tr("Playlist Name:"), QLineEdit::Normal, m_tabs->tabText(index), &success);

            if(success && !text.isEmpty()) {
                m_playlistHandler->renamePlaylist(id, text);
            }
        });

        auto* removePlaylist = new QAction("Remove Playlist", menu);
        QObject::connect(removePlaylist, &QAction::triggered, this, [this, id]() {
            m_playlistHandler->removePlaylist(id);
        });

        menu->addAction(renamePlaylist);
        menu->addAction(removePlaylist);
    }
    menu->addAction(createPlaylist);

    menu->popup(mapToGlobal(point));
}

// void PlaylistTabs::layoutEditingMenu(Utils::ActionContainer* menu)
//{
//     auto menuId   = id().append(tr("&Add"));
//     auto* addMenu = m_actionManager->createMenu(menuId);
//     addMenu->menu()->setTitle(tr("&Add"));

//    auto widgets = m_widgetFactory->registeredWidgets();
//    for(const auto& widget : widgets) {
//        auto* parentMenu = addMenu;
//        for(const auto& subMenu : widget.second.subMenus) {
//            const Utils::Id id = Utils::Id{addMenu->id()}.append(subMenu);
//            auto* childMenu    = m_actionManager->actionContainer(id);
//            if(!childMenu) {
//                childMenu = m_actionManager->createMenu(id);
//                childMenu->menu()->setTitle(subMenu);
//                parentMenu->addMenu(childMenu);
//            }
//            parentMenu = childMenu;
//        }
//        auto* addWidget = new QAction(widget.second.name, parentMenu);
//        QAction::connect(addWidget, &QAction::triggered, this, [this, widget] {
//            FyWidget* newWidget = m_widgetFactory->make(widget.first);
//            m_layout->addWidget(newWidget);
//        });
//        parentMenu->addAction(addWidget);
//    }

//    if(m_layout->children().count() > 1) {
//        addMenu->menu()->setEnabled(false);
//    }

//    menu->addMenu(addMenu);
//}

void PlaylistTabs::tabChanged(int index)
{
    const int id = m_tabs->tabData(index).toInt();
    if(id >= 0) {
        m_playlistHandler->changeCurrentPlaylist(id);
    }
}

void PlaylistTabs::playlistChanged(Core::Playlist::Playlist* playlist)
{
    for(int i = 0; i < m_tabs->count(); ++i) {
        if(m_tabs->tabData(i).toInt() == playlist->id()) {
            m_tabs->setCurrentIndex(i);
        }
    }
}

void PlaylistTabs::playlistRenamed(Core::Playlist::Playlist* playlist)
{
    for(int i = 0; i < m_tabs->count(); ++i) {
        if(m_tabs->tabData(i).toInt() == playlist->id()) {
            m_tabs->setTabText(i, playlist->name());
        }
    }
}
} // namespace Fy::Gui::Widgets
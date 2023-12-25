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

#include "playlistwidget.h"

#include "internalguisettings.h"
#include "playlistcolumnregistry.h"
#include "playlistcontroller.h"
#include "playlistdelegate.h"
#include "playlisthistory.h"
#include "playlistview.h"
#include "playlistwidget_p.h"
#include "presetregistry.h"

#include "core/library/sortingregistry.h"

#include <core/library/musiclibrary.h>
#include <core/library/tracksort.h>
#include <core/playlist/playlistmanager.h>
#include <gui/guiconstants.h>
#include <gui/guisettings.h>
#include <gui/trackselectioncontroller.h>
#include <utils/actions/actioncontainer.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/async.h>
#include <utils/recursiveselectionmodel.h>
#include <utils/widgets/autoheaderview.h>

#include <QActionGroup>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QProgressDialog>

#include <QCoro/QCoroCore>

#include <stack>

using namespace Qt::Literals::StringLiterals;

namespace {
void expandTree(QTreeView* view, QAbstractItemModel* model, const QModelIndex& parent, int first, int last)
{
    while(first <= last) {
        const QModelIndex child = model->index(first, 0, parent);
        view->expand(child);

        const int childCount = model->rowCount(child);
        if(childCount > 0) {
            expandTree(view, model, child, 0, childCount - 1);
        }

        ++first;
    }
}

Fooyin::TrackList getAllTracks(QAbstractItemModel* model, const QModelIndexList& indexes)
{
    Fooyin::TrackList tracks;

    std::stack<QModelIndex> indexStack;
    for(const QModelIndex& index : indexes | std::views::reverse) {
        indexStack.push(index);
    }

    while(!indexStack.empty()) {
        const QModelIndex currentIndex = indexStack.top();
        indexStack.pop();

        while(model->canFetchMore(currentIndex)) {
            model->fetchMore(currentIndex);
        }

        const int rowCount = model->rowCount(currentIndex);
        if(rowCount == 0
           && currentIndex.data(Fooyin::PlaylistItem::Role::Type).toInt() == Fooyin::PlaylistItem::Track) {
            tracks.push_back(currentIndex.data(Fooyin::PlaylistItem::Role::ItemData).value<Fooyin::Track>());
        }
        else {
            for(int row{rowCount - 1}; row >= 0; --row) {
                const QModelIndex childIndex = model->index(row, 0, currentIndex);
                indexStack.push(childIndex);
            }
        }
    }
    return tracks;
}
} // namespace

namespace Fooyin {
using namespace Settings::Gui::Internal;

PlaylistWidgetPrivate::PlaylistWidgetPrivate(PlaylistWidget* self, ActionManager* actionManager,
                                             PlaylistController* playlistController,
                                             PlaylistColumnRegistry* columnRegistry, MusicLibrary* library,
                                             SettingsManager* settings)
    : self{self}
    , actionManager{actionManager}
    , selectionController{playlistController->selectionController()}
    , columnRegistry{columnRegistry}
    , library{library}
    , settings{settings}
    , settingsDialog{settings->settingsDialog()}
    , playlistController{playlistController}
    , layout{new QHBoxLayout(self)}
    , model{new PlaylistModel(library, settings, self)}
    , playlistView{new PlaylistView(self)}
    , header{new AutoHeaderView(Qt::Horizontal, self)}
    , columnMode{false}
    , playlistContext{new WidgetContext(self, Context{Constants::Context::Playlist}, self)}
    , removeTrackAction{new QAction(tr("Remove"), self)}
{
    layout->setContentsMargins(0, 0, 0, 0);

    playlistView->setHeader(header);
    header->setStretchEnabled(true);
    header->setSectionsMovable(true);
    header->setFirstSectionMovable(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);

    playlistView->setModel(model);
    playlistView->setItemDelegate(new PlaylistDelegate(self));

    playlistView->setSelectionModel(new RecursiveSelectionModel(model, this));

    layout->addWidget(playlistView);

    setHeaderHidden(settings->value<PlaylistHeader>());
    setScrollbarHidden(settings->value<PlaylistScrollBar>());

    changePreset(playlistController->presetRegistry()->itemByName(settings->value<PlaylistCurrentPreset>()));

    setupConnections();
    setupActions();

    model->changeFirstColumn(header->logicalIndex(0));
    model->setCurrentPlaylistIsActive(playlistController->currentIsActive());
}

void PlaylistWidgetPrivate::setupConnections()
{
    QObject::connect(playlistView->header(), &QHeaderView::customContextMenuRequested, this,
                     &PlaylistWidgetPrivate::customHeaderMenuRequested);
    QObject::connect(playlistView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                     &PlaylistWidgetPrivate::selectionChanged);
    QObject::connect(playlistView, &QAbstractItemView::doubleClicked, this, &PlaylistWidgetPrivate::doubleClicked);

    QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, playlistView, &QAbstractItemView::clearSelection);
    QObject::connect(model, &PlaylistModel::playlistTracksChanged, this, &PlaylistWidgetPrivate::playlistTracksChanged);
    QObject::connect(model, &PlaylistModel::filesDropped, this, &PlaylistWidgetPrivate::scanDroppedTracks);
    QObject::connect(model, &PlaylistModel::tracksInserted, this, &PlaylistWidgetPrivate::tracksInserted);
    QObject::connect(model, &PlaylistModel::tracksMoved, this, &PlaylistWidgetPrivate::tracksMoved);
    QObject::connect(model, &QAbstractItemModel::modelReset, this, &PlaylistWidgetPrivate::resetTree);
    QObject::connect(model, &QAbstractItemModel::rowsInserted, this,
                     [this](const QModelIndex& parent, int first, int last) {
                         expandTree(playlistView, model, parent, first, last);
                     });

    QObject::connect(header, &QHeaderView::sectionMoved, this,
                     [this]() { model->changeFirstColumn(header->logicalIndex(0)); });

    QObject::connect(playlistController, &PlaylistController::currentPlaylistChanged, this,
                     &PlaylistWidgetPrivate::changePlaylist);
    QObject::connect(playlistController, &PlaylistController::currentTrackChanged, model,
                     &PlaylistModel::currentTrackChanged);
    QObject::connect(playlistController, &PlaylistController::playStateChanged, model,
                     &PlaylistModel::playStateChanged);
    QObject::connect(playlistController->playlistHandler(), &PlaylistManager::activeTrackChanged, this,
                     &PlaylistWidgetPrivate::followCurrentTrack);
    QObject::connect(playlistController->playlistHandler(), &PlaylistManager::playlistTracksAdded, this,
                     &PlaylistWidgetPrivate::playlistTracksAdded);
    QObject::connect(playlistController->presetRegistry(), &PresetRegistry::presetChanged, this,
                     &PlaylistWidgetPrivate::onPresetChanged);

    settings->subscribe<PlaylistHeader>(this, &PlaylistWidgetPrivate::setHeaderHidden);
    settings->subscribe<PlaylistScrollBar>(this, &PlaylistWidgetPrivate::setScrollbarHidden);
    settings->subscribe<PlaylistCurrentPreset>(this, [this](const QString& presetName) {
        const auto preset = playlistController->presetRegistry()->itemByName(presetName);
        changePreset(preset);
    });
}

void PlaylistWidgetPrivate::setupActions()
{
    actionManager->addContextObject(playlistContext);

    auto* editMenu = actionManager->actionContainer(Constants::Menus::Edit);

    auto* undo    = new QAction(tr("Undo"), this);
    auto* undoCmd = actionManager->registerAction(undo, Constants::Actions::Undo, playlistContext->context());
    undoCmd->setDefaultShortcut(QKeySequence::Undo);
    editMenu->addAction(undoCmd);
    QObject::connect(undo, &QAction::triggered, this, [this]() { playlistController->undoPlaylistChanges(); });
    QObject::connect(playlistController, &PlaylistController::playlistHistoryChanged, this,
                     [this, undo]() { undo->setEnabled(playlistController->canUndo()); });
    undo->setEnabled(playlistController->canUndo());

    auto* redo    = new QAction(tr("Redo"), this);
    auto* redoCmd = actionManager->registerAction(redo, Constants::Actions::Redo, playlistContext->context());
    redoCmd->setDefaultShortcut(QKeySequence::Redo);
    editMenu->addAction(redoCmd);
    QObject::connect(redo, &QAction::triggered, this, [this]() { playlistController->redoPlaylistChanges(); });
    QObject::connect(playlistController, &PlaylistController::playlistHistoryChanged, this,
                     [this, redo]() { redo->setEnabled(playlistController->canRedo()); });
    redo->setEnabled(playlistController->canRedo());

    editMenu->addSeparator();

    auto* clear = new QAction(PlaylistWidget::tr("&Clear"), self);
    editMenu->addAction(actionManager->registerAction(clear, Constants::Actions::Clear, playlistContext->context()));
    QObject::connect(clear, &QAction::triggered, this, [this]() {
        if(auto* playlist = playlistController->currentPlaylist()) {
            playlist->clear();
            model->reset(currentPreset, columns, playlist);
        }
    });

    auto* selectAll = new QAction(PlaylistWidget::tr("&Select All"), self);
    auto* selectAllCmd
        = actionManager->registerAction(selectAll, Constants::Actions::SelectAll, playlistContext->context());
    selectAllCmd->setDefaultShortcut(QKeySequence::SelectAll);
    editMenu->addAction(selectAllCmd);
    QObject::connect(selectAll, &QAction::triggered, this, [this]() {
        while(model->canFetchMore({})) {
            model->fetchMore({});
        }
        playlistView->selectAll();
    });

    removeTrackAction = new QAction(tr("Remove"), self);
    removeTrackAction->setEnabled(false);
    auto* removeCmd
        = actionManager->registerAction(removeTrackAction, Constants::Actions::Remove, playlistContext->context());
    removeCmd->setDefaultShortcut(QKeySequence::Delete);
    QObject::connect(removeTrackAction, &QAction::triggered, this, [this]() { tracksRemoved(); });
}

void PlaylistWidgetPrivate::onPresetChanged(const PlaylistPreset& preset)
{
    if(currentPreset.id == preset.id) {
        changePreset(preset);
    }
}

void PlaylistWidgetPrivate::changePreset(const PlaylistPreset& preset)
{
    currentPreset = preset;
    model->reset(currentPreset, columns, playlistController->currentPlaylist());
}

void PlaylistWidgetPrivate::changePlaylist(Playlist* playlist) const
{
    model->reset(currentPreset, columns, playlist);
    //    playlistView->setFocus(Qt::ActiveWindowFocusReason);
}

void PlaylistWidgetPrivate::resetTree() const
{
    if(columnMode) {
        // extendHeaders(playlistView, model, {}, model->firstColumn(), true);
    }
    playlistView->expandAll();
    playlistView->scrollToTop();
}

bool PlaylistWidgetPrivate::isHeaderHidden() const
{
    return playlistView->isHeaderHidden();
}

bool PlaylistWidgetPrivate::isScrollbarHidden() const
{
    return playlistView->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff;
}

void PlaylistWidgetPrivate::setHeaderHidden(bool showHeader) const
{
    playlistView->setHeaderHidden(!showHeader);
}

void PlaylistWidgetPrivate::setScrollbarHidden(bool showScrollBar) const
{
    playlistView->setVerticalScrollBarPolicy(!showScrollBar ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
}

void PlaylistWidgetPrivate::selectionChanged() const
{
    TrackList tracks;
    int firstIndex{-1};

    const QModelIndexList indexes = playlistView->selectionModel()->selectedIndexes();

    for(const QModelIndex& index : indexes) {
        const auto type = index.data(PlaylistItem::Type).toInt();
        if(type == PlaylistItem::Track) {
            tracks.push_back(index.data(PlaylistItem::Role::ItemData).value<Track>());
            const int trackIndex = index.data(PlaylistItem::Role::Index).toInt();
            firstIndex           = firstIndex >= 0 ? std::min(firstIndex, trackIndex) : trackIndex;
        }
    }

    selectionController->changeSelectedTracks(firstIndex, tracks);

    if(tracks.empty()) {
        removeTrackAction->setEnabled(false);
        return;
    }

    if(settings->value<Settings::Gui::PlaybackFollowsCursor>()) {
        if(auto* currentPlaylist = playlistController->currentPlaylist()) {
            if(currentPlaylist->currentTrackIndex() != firstIndex) {
                if(playlistController->playState() != PlayState::Playing) {
                    currentPlaylist->changeCurrentTrack(firstIndex);
                }
                else {
                    currentPlaylist->scheduleNextIndex(firstIndex);
                }
                playlistController->playlistHandler()->schedulePlaylist(currentPlaylist);
            }
        }
    }
    removeTrackAction->setEnabled(true);
}

void PlaylistWidgetPrivate::playlistTracksChanged(int index) const
{
    const TrackList tracks = getAllTracks(model, {QModelIndex{}});

    if(!tracks.empty()) {
        const QModelIndexList selected = playlistView->selectionModel()->selectedIndexes();
        if(!selected.empty()) {
            const int firstIndex           = selected.front().data(PlaylistItem::Role::Index).toInt();
            const TrackList selectedTracks = getAllTracks(model, selected);
            selectionController->changeSelectedTracks(firstIndex, selectedTracks);
        }
    }

    playlistController->playlistHandler()->clearSchedulePlaylist();

    if(auto* playlist = playlistController->currentPlaylist()) {
        playlist->replaceTracks(tracks);
        if(index >= 0) {
            playlist->changeCurrentTrack(index);
        }
        model->updateHeader(playlist);
    }
}

QCoro::Task<void> PlaylistWidgetPrivate::scanDroppedTracks(TrackList tracks, int index)
{
    playlistView->setFocus(Qt::ActiveWindowFocusReason);

    auto* progress = new QProgressDialog("Reading tracks...", "Abort", 0, 100, self);
    progress->setWindowModality(Qt::WindowModal);

    ScanRequest* request = library->scanTracks(tracks);

    progress->show();

    QObject::connect(library, &MusicLibrary::scanProgress, progress, [request, progress](int id, int percent) {
        if(id != request->id) {
            return;
        }

        progress->setValue(percent);
        if(progress->wasCanceled()) {
            request->cancel();
            progress->close();
        }
    });

    const TrackList scannedTracks = co_await qCoro(library, &MusicLibrary::tracksScanned);

    progress->deleteLater();

    auto* insertCmd = new InsertTracks(model, {{index, scannedTracks}});
    playlistController->addToHistory(insertCmd);
}

void PlaylistWidgetPrivate::tracksInserted(const TrackGroups& tracks) const
{
    auto* insertCmd = new InsertTracks(model, tracks);
    playlistController->addToHistory(insertCmd);

    playlistView->setFocus(Qt::ActiveWindowFocusReason);
}

void PlaylistWidgetPrivate::tracksRemoved() const
{
    const auto selected = playlistView->selectionModel()->selectedIndexes();

    QModelIndexList trackSelection;
    IndexSet indexes;

    for(const QModelIndex& index : selected) {
        if(index.isValid() && index.data(PlaylistItem::Type).toInt() == PlaylistItem::Track) {
            trackSelection.push_back(index);
            indexes.emplace(index.data(PlaylistItem::Index).toInt());
        }
    }

    playlistController->playlistHandler()->clearSchedulePlaylist();

    if(auto* playlist = playlistController->currentPlaylist()) {
        playlist->removeTracks(indexes);

        auto* delCmd = new RemoveTracks(model, model->saveTrackGroups(trackSelection));
        playlistController->addToHistory(delCmd);

        model->updateHeader(playlist);
    }
}

void PlaylistWidgetPrivate::tracksMoved(const MoveOperation& operation) const
{
    auto* moveCmd = new MoveTracks(model, operation);
    playlistController->addToHistory(moveCmd);

    playlistView->setFocus(Qt::ActiveWindowFocusReason);
}

void PlaylistWidgetPrivate::playlistTracksAdded(Playlist* playlist, const TrackList& tracks, int index) const
{
    if(playlistController->currentPlaylist() == playlist) {
        auto* insertCmd = new InsertTracks(model, {{index, tracks}});
        playlistController->addToHistory(insertCmd);
    }
}

QCoro::Task<void> PlaylistWidgetPrivate::toggleColumnMode()
{
    columnMode = !columnMode;
    columns.clear();

    if(columnMode) {
        columns.push_back(columnRegistry->itemByName("Track"));
        columns.push_back(columnRegistry->itemByName("Title"));
        columns.push_back(columnRegistry->itemByName("Artist"));
        columns.push_back(columnRegistry->itemByName("Album"));
        columns.push_back(columnRegistry->itemByName("Duration"));
    }

    model->reset(currentPreset, columns, playlistController->currentPlaylist());

    if(columnMode) {
        co_await qCoro(header, &QHeaderView::sectionCountChanged);
        header->resetSections();
    }
}

void PlaylistWidgetPrivate::customHeaderMenuRequested(QPoint pos)
{
    auto* menu = new QMenu(self);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    auto* playlistMenu = new QMenu(tr("Playlists"), menu);

    const auto currentPlaylist = playlistController->currentPlaylist();
    const auto playlists       = playlistController->playlists();

    for(const auto& playlist : playlists) {
        if(playlist != currentPlaylist) {
            auto* switchPl = new QAction(playlist->name(), playlistMenu);
            const int id   = playlist->id();
            QObject::connect(switchPl, &QAction::triggered, self,
                             [this, id]() { playlistController->changeCurrentPlaylist(id); });
            playlistMenu->addAction(switchPl);
        }
    }

    menu->addMenu(playlistMenu);

    if(columnMode) {
        auto* filterList = new QActionGroup{menu};
        filterList->setExclusionPolicy(QActionGroup::ExclusionPolicy::None);

        auto hasColumn = [this](int id) {
            return std::ranges::any_of(columns, [id](const PlaylistColumn& column) { return column.id == id; });
        };

        for(const auto& [filterIndex, column] : columnRegistry->items()) {
            auto* columnAction = new QAction(column.name, menu);
            columnAction->setData(column.id);
            columnAction->setCheckable(true);
            columnAction->setChecked(hasColumn(column.id));
            menu->addAction(columnAction);
            filterList->addAction(columnAction);
        }

        menu->setDefaultAction(filterList->checkedAction());
        QObject::connect(filterList, &QActionGroup::triggered, self, [this](QAction* action) {
            const int columnId = action->data().toInt();
            if(action->isChecked()) {
                const PlaylistColumn column = columnRegistry->itemById(action->data().toInt());
                if(column.isValid()) {
                    columns.push_back(column);
                    changePreset(currentPreset);
                }
            }
            else {
                std::erase_if(columns,
                              [columnId](const PlaylistColumn& filterCol) { return filterCol.id == columnId; });
                changePreset(currentPreset);
            }
        });

        header->addHeaderContextMenu(menu, self->mapToGlobal(pos));
        menu->addSeparator();
    }

    auto* columnModeAction = new QAction(tr("Multi-column Mode"), menu);
    columnModeAction->setCheckable(true);
    columnModeAction->setChecked(columnMode);
    QObject::connect(columnModeAction, &QAction::triggered, self, [this]() { toggleColumnMode(); });
    menu->addAction(columnModeAction);

    auto* presetsMenu = new QMenu(PlaylistWidget::tr("Presets"), menu);

    const auto& presets = playlistController->presetRegistry()->items();

    for(const auto& [index, preset] : presets) {
        const QString name = preset.name;
        auto* switchPreset = new QAction(name, presetsMenu);
        if(preset == currentPreset) {
            presetsMenu->setDefaultAction(switchPreset);
        }
        QObject::connect(switchPreset, &QAction::triggered, self,
                         [this, name]() { settings->set<PlaylistCurrentPreset>(name); });
        presetsMenu->addAction(switchPreset);
    }
    menu->addMenu(presetsMenu);

    menu->popup(self->mapToGlobal(pos));
}

void PlaylistWidgetPrivate::doubleClicked(const QModelIndex& /*index*/) const
{
    selectionController->executeAction(TrackAction::Play);
    model->currentTrackChanged(playlistController->currentTrack());
    playlistView->clearSelection();
}

void PlaylistWidgetPrivate::followCurrentTrack(const Track& track, int index) const
{
    if(!settings->value<Settings::Gui::CursorFollowsPlayback>()) {
        return;
    }

    if(playlistController->playState() != PlayState::Playing) {
        return;
    }

    if(!track.isValid()) {
        return;
    }

    const QModelIndex modelIndex = model->indexAtTrackIndex(index);
    if(!modelIndex.isValid()) {
        return;
    }

    const QRect indexRect    = playlistView->visualRect(modelIndex);
    const QRect viewportRect = playlistView->viewport()->rect();

    if((indexRect.top() < 0) || (viewportRect.bottom() - indexRect.bottom() < 0)) {
        playlistView->scrollTo(modelIndex, QAbstractItemView::PositionAtCenter);
    }

    playlistView->setCurrentIndex(modelIndex);
}

QCoro::Task<void> PlaylistWidgetPrivate::changeSort(QString script)
{
    /* if(playlistView->selectionModel()->hasSelection()) { }
     else*/
    if(auto* playlist = playlistController->currentPlaylist()) {
        const auto sortedTracks = co_await Utils::asyncExec(
            [&script, &playlist]() { return Sorting::calcSortTracks(script, playlist->tracks()); });
        playlist->replaceTracks(sortedTracks);
        changePlaylist(playlist);
    }
    co_return;
}

void PlaylistWidgetPrivate::addSortMenu(QMenu* parent)
{
    auto* sortMenu = new QMenu(PlaylistWidget::tr("Sort"), parent);

    const auto& groups = playlistController->sortRegistry()->items();
    for(const auto& [index, script] : groups) {
        auto* switchSort = new QAction(script.name, sortMenu);
        QObject::connect(switchSort, &QAction::triggered, self, [this, script]() { changeSort(script.script); });
        sortMenu->addAction(switchSort);
    }
    parent->addMenu(sortMenu);
}

PlaylistWidget::PlaylistWidget(ActionManager* actionManager, PlaylistController* playlistController,
                               PlaylistColumnRegistry* columnRegistry, MusicLibrary* library, SettingsManager* settings,
                               QWidget* parent)
    : FyWidget{parent}
    , p{std::make_unique<PlaylistWidgetPrivate>(this, actionManager, playlistController, columnRegistry, library,
                                                settings)}
{
    setObjectName(PlaylistWidget::name());
}

PlaylistWidget::~PlaylistWidget() = default;

QString PlaylistWidget::name() const
{
    return QStringLiteral("Playlist");
}

void PlaylistWidget::saveLayoutData(QJsonObject& layout)
{
    if(p->columnMode) {
        QStringList columns;
        std::ranges::transform(p->columns, std::back_inserter(columns),
                               [](const auto& column) { return QString::number(column.id); });

        layout["Columns"_L1] = columns.join("|"_L1);
    }

    QByteArray state = p->header->saveHeaderState();
    state            = qCompress(state, 9);

    layout["State"_L1] = QString::fromUtf8(state.toBase64());
}

void PlaylistWidget::loadLayoutData(const QJsonObject& layout)
{
    if(layout.contains("Columns"_L1)) {
        p->columns.clear();
        const QString columnNames = layout.value("Columns"_L1).toString();
        const QStringList columns = columnNames.split("|"_L1);
        std::ranges::transform(columns, std::back_inserter(p->columns),
                               [this](const QString& column) { return p->columnRegistry->itemById(column.toInt()); });
        p->columnMode = !p->columns.empty();
    }

    p->model->reset(p->currentPreset, p->columns, p->playlistController->currentPlaylist());

    if(layout.contains("State"_L1)) {
        auto state = QByteArray::fromBase64(layout.value("State"_L1).toString().toUtf8());

        if(state.isEmpty()) {
            return;
        }

        state = qUncompress(state);

        p->header->restoreHeaderState(state);
    }
}

void PlaylistWidget::contextMenuEvent(QContextMenuEvent* event)
{
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if(auto* removeCmd = p->actionManager->command(Constants::Actions::Remove)) {
        menu->addAction(removeCmd->action());
    }

    menu->addSeparator();

    p->addSortMenu(menu);
    p->selectionController->addTrackContextMenu(menu);

    menu->popup(event->globalPos());
}

void PlaylistWidget::keyPressEvent(QKeyEvent* event)
{
    const auto key = event->key();

    if(key == Qt::Key_Enter || key == Qt::Key_Return) {
        if(p->selectionController->hasTracks()) {
            p->selectionController->executeAction(TrackAction::Play);
        }
        p->playlistView->clearSelection();
    }
    QWidget::keyPressEvent(event);
}
} // namespace Fooyin

#include "moc_playlistwidget.cpp"

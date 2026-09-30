#include "folder_select.h"
#include "mainwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QIcon>
#include <QCheckBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QToolTip>

FolderSelect::FolderSelect(MainWindow *window) : m_window(window) {}

void FolderSelect::loadMusic(const QString &directory, bool remember)
    {
        m_window->m_musicBaseDirectory = QFileInfo(directory).absoluteFilePath();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_window->m_scanning->loadMusic(directory);
        m_window->m_albums = m_window->m_scanning->albums();
        m_window->m_playlists = m_window->m_scanning->playlists();
        m_window->m_filteredAlbums = m_window->m_albums;
        QApplication::restoreOverrideCursor();

        if (m_window->m_albums.isEmpty() && m_window->m_playlists.isEmpty()) {
            QMessageBox::information(m_window, "No Music Found", "No supported music files or M3U playlists were found.");
            return;
        }

        if (remember && m_window->m_rememberFolder && m_window->m_rememberFolder->isChecked())
            emit m_window->rememberFolderRequested(m_window->m_musicBaseDirectory);

        m_window->m_search->clear();
        m_window->m_stack->setCurrentWidget(m_window->m_albumsPage);
        emit m_window->albumsGridRebuildRequested();
    }

void FolderSelect::createStartChoiceButtons(QVBoxLayout *content)
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(40);
        row->setAlignment(Qt::AlignCenter);

        for (const auto &data : {QPair<QString, QString>{"folder", "folder-music"},
                                 {"music", "audio-x-generic"}}) {
            auto *button = new QToolButton;
            button->setText(data.first);
            button->setIcon(QIcon::fromTheme(data.second,
                QIcon::fromTheme(data.first == "folder" ? "folder-open" : "multimedia-player")));
            button->setIconSize(QSize(72, 72));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setFixedSize(120, 120);
            button->setStyleSheet(
                "QToolButton { background: transparent; border: none; padding: 8px; }"
                "QToolButton:hover { background: rgba(128,128,128,35); border-radius: 8px; }");
            row->addWidget(button);

            if (data.first == "folder")
                QObject::connect(button, &QAbstractButton::clicked, m_window,
                                 [this]() { m_window->m_folderSelect->chooseFolder(); });
            else
                QObject::connect(button, &QAbstractButton::clicked, m_window,
                                 [this]() { m_window->m_folderSelect->chooseFile(); });
        }
        content->addLayout(row);
    }

void FolderSelect::createStartContent(QVBoxLayout *layout)
    {
        auto *content = new QVBoxLayout;
        content->setAlignment(Qt::AlignCenter);
        content->setSpacing(15);

        auto *logo = new QLabel;
        logo->setPixmap(QIcon(QStringLiteral(":/sangeet.svg")).pixmap(220, 220));
        logo->setAlignment(Qt::AlignCenter);
        logo->setMinimumSize(220, 220);
        content->addWidget(logo, 0, Qt::AlignCenter);

        auto *title = new QLabel("Sangeet");
        QFont font = title->font();
        font.setPointSize(30);
        font.setBold(true);
        title->setFont(font);
        title->setAlignment(Qt::AlignCenter);
        content->addWidget(title);

        auto *description = new QLabel("Your music. Your library. Your sound.");
        description->setAlignment(Qt::AlignCenter);
        font.setPointSize(12);
        font.setBold(false);
        description->setFont(font);
        content->addWidget(description);

        createStartChoiceButtons(content);

        auto *audioOptions = new QVBoxLayout;
        audioOptions->setSpacing(8);
        audioOptions->setAlignment(Qt::AlignCenter);
        
        auto *waveform = new QCheckBox("Waveform");
        waveform->setChecked(m_window->m_settings.progressBarMode() == 1);
        waveform->setCursor(Qt::PointingHandCursor);
        
        QObject::connect(waveform, &QCheckBox::toggled, m_window,
                         [this](bool checked) {
                             m_window->m_settings.saveProgressBarMode(checked ? 1 : 0);
                         });
        
        audioOptions->addWidget(waveform, 0, Qt::AlignCenter);
        
        auto *alsaContainer = new QWidget;
        auto *alsaRow = new QHBoxLayout(alsaContainer);
        alsaRow->setContentsMargins(0, 0, 0, 0);
        alsaRow->setSpacing(8);
        
        auto *alsa = new QLineEdit;
        alsa->setPlaceholderText("ALSA card name");
        alsa->setText(m_window->m_settings.alsaCardName());
        alsa->setFixedWidth(220);
        
        QObject::connect(alsa, &QLineEdit::editingFinished, m_window,
                         [this, alsa]() {
                             m_window->m_settings.saveAlsaCardName(alsa->text().trimmed());
                             
                             QToolTip::showText(
                                 alsa->mapToGlobal(QPoint(0, alsa->height())),
                                                "ALSA card saved",
                                                alsa);
                         });
        
        alsaRow->addWidget(alsa);
        auto *alsaHelp = new QToolButton;
        alsaHelp->setText("🛈");
        alsaHelp->setAutoRaise(true);
        alsaHelp->setCursor(Qt::PointingHandCursor);
        alsaHelp->setToolTip("Run aplay -L in terminal for listing all ALSA cards.\n used for bypassing the pipewire \n so that it output bitperfect.");
        alsaHelp->setFixedSize(28, 28);
        
        alsaRow->addWidget(alsaHelp);
        
        audioOptions->addWidget(alsaContainer, 0, Qt::AlignCenter);
        
        content->addLayout(audioOptions);
        layout->addLayout(content, 1);
    }

void FolderSelect::connectStartPage()
    {
        QObject::connect(m_window->m_rememberFolder, &QCheckBox::toggled, m_window, [this](bool checked) {
            if (checked) {
                if (!m_window->m_musicBaseDirectory.isEmpty())
                    emit m_window->rememberFolderRequested(m_window->m_musicBaseDirectory);
            } else {
                m_window->m_folderSelect->clearRememberedFolder();
            }
        });
    }

void FolderSelect::createStartPage()
    {
        m_window->m_startPage = new QWidget;
        auto *layout = new QVBoxLayout(m_window->m_startPage);
        layout->setContentsMargins(15, 15, 15, 15);
        layout->setSpacing(15);

        auto *topBar = new QHBoxLayout;
        topBar->addStretch();
        topBar->addWidget(m_window->m_folderSelect->createStartHomeButton());
        layout->addLayout(topBar);

        createStartContent(layout);
        connectStartPage();
    }

void FolderSelect::goHome()
    {
            m_window->m_folderSelect->collapseSearch(m_window->m_search);
            m_window->m_folderSelect->collapseSearch(m_window->m_playlistsSearch);
            m_window->m_folderSelect->collapseSearch(m_window->m_tracksSearch);
            m_window->m_stack->setCurrentWidget(m_window->m_startPage);
        
    }

void FolderSelect::goToRememberedFolder()
    {
            const QString directory = m_window->m_settings.rememberedFolder();
            if (!directory.isEmpty())
                m_window->m_folderSelect->loadMusic(directory, false);
        
    }

QToolButton *FolderSelect::createStartHomeButton()
    {
            auto *button = m_window->m_folderSelect->createHomeButton();
            button->setToolTip(QStringLiteral("Back to remembered folder"));
            QObject::connect(button, &QToolButton::clicked, m_window, [this]() { m_window->m_folderSelect->goToRememberedFolder(); });
            return button;
        
    }

void FolderSelect::collapseSearch(QLineEdit *edit)
    {
            if (!edit || !edit->isVisible())
                return;
            edit->clearFocus();
            edit->setVisible(false);
        
    }

QToolButton *FolderSelect::createSearchButton(QLineEdit *edit)
    {
            auto *button = new QToolButton;
            const QIcon icon = QIcon::fromTheme(QStringLiteral("system-search"),
                                                 QIcon::fromTheme(QStringLiteral("edit-find")));
            if (!icon.isNull()) {
                button->setIcon(icon);
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            } else {
                button->setText(QStringLiteral("⌕"));
                button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            }
            button->setIconSize(QSize(22, 22));
            button->setAutoRaise(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setToolTip(QStringLiteral("Search"));
            button->setFixedSize(40, 40);
            edit->setVisible(false);
            edit->setFixedWidth(240);
            const QIcon clearIcon = QIcon::fromTheme(QStringLiteral("edit-clear"),
                                                      QIcon::fromTheme(QStringLiteral("edit-delete")));
            if (!clearIcon.isNull()) {
                QAction *clearAction = edit->addAction(clearIcon, QLineEdit::TrailingPosition);
                QObject::connect(clearAction, &QAction::triggered, edit, &QLineEdit::clear);
                clearAction->setToolTip(QStringLiteral("Clear search"));
            }
    
            QObject::connect(button, &QToolButton::clicked, m_window, [this, edit]() {
                if (edit->isVisible()) {
                    m_window->m_folderSelect->collapseSearch(edit);
                    return;
                }
                m_window->m_folderSelect->collapseSearch(m_window->m_search);
                m_window->m_folderSelect->collapseSearch(m_window->m_playlistsSearch);
                m_window->m_folderSelect->collapseSearch(m_window->m_tracksSearch);
                edit->setVisible(true);
                edit->setFocus();
            });
            return button;
        
    }

QToolButton *FolderSelect::createHomeButton()
    {
            auto *button = new QToolButton;
            const QIcon icon = QIcon::fromTheme(QStringLiteral("go-home"),
                                                 QIcon::fromTheme(QStringLiteral("user-home")));
            if (!icon.isNull()) {
                button->setIcon(icon);
                button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            } else {
                button->setText(QStringLiteral("⌂"));
                button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            }
            button->setIconSize(QSize(22, 22));
            button->setAutoRaise(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setToolTip(QStringLiteral("Home"));
            button->setFixedSize(40, 40);
            QObject::connect(button, &QToolButton::clicked, m_window, [this]() { m_window->m_folderSelect->goHome(); });
            return button;
        
    }

void FolderSelect::restoreRememberedFolder()
    {
            m_window->m_folderSelect->loadRememberedFolderSetting();
        
    }

void FolderSelect::openPath(const QString &path)
    {
            const QFileInfo info(path);
            if (!info.exists())
                return;
    
            if (info.isDir()) {
                m_window->m_folderSelect->loadMusic(info.absoluteFilePath(), false);
                return;
            }
    
            if (isPlaylistFile(info.absoluteFilePath())) {
                m_window->m_folderSelect->loadMusic(info.absolutePath(), false);
                const QString playlistPath = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
                for (const Playlist &playlist : m_window->m_playlists) {
                    const QString scannedPath = QFileInfo(playlist.path).canonicalFilePath().isEmpty() ? QFileInfo(playlist.path).absoluteFilePath() : QFileInfo(playlist.path).canonicalFilePath();
                    if (scannedPath == playlistPath) {
                        emit m_window->openPlaylistRequested(playlist);
                        return;
                    }
                }
                return;
            }
    
            if (!isMusicFile(info.absoluteFilePath()))
                return;
    
            const QString file = info.absoluteFilePath();
            m_window->m_folderSelect->loadMusic(info.absolutePath(), false);
    
            for (const Album &album : m_window->m_albums) {
                for (int i = 0; i < album.tracks.size(); ++i) {
                    if (QFileInfo(album.tracks[i].path).absoluteFilePath() == file) {
                        emit m_window->openAlbumRequested(album, i);
                        return;
                    }
                }
            }
        
    }

void FolderSelect::chooseFolder()
    {
            const QString directory = QFileDialog::getExistingDirectory(
                m_window, "Choose Music Folder");
            if (!directory.isEmpty())
                m_window->m_folderSelect->loadMusic(directory, true);
        
    }

void FolderSelect::chooseFile()
    {
            const QString file = QFileDialog::getOpenFileName(
                m_window,
                "Choose Music File",
                QString(),
                "Music Files (*.mp3 *.flac *.ogg *.oga *.opus *.wav *.m4a *.aac *.wma *.ape *.wv *.mpc)"
            );
    
            if (!file.isEmpty())
                m_window->m_folderSelect->openPath(file);
        
    }

void FolderSelect::loadRememberedFolderSetting()
    {
            const QString directory = m_window->m_settings.rememberedFolder();
            if (m_window->m_rememberFolder) {
                QSignalBlocker blocker(m_window->m_rememberFolder);
                m_window->m_rememberFolder->setChecked(m_window->m_settings.rememberFolderEnabled());
            }
            if (!directory.isEmpty())
                m_window->m_folderSelect->loadMusic(directory, false);
        
    }

void FolderSelect::saveRememberedFolder(const QString &directory)
    {
            m_window->m_settings.saveRememberedFolder(directory);
        
    }

void FolderSelect::clearRememberedFolder()
    {
            m_window->m_settings.clearRememberedFolder();
        
    }

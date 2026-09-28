#pragma once

#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QString>

class MainWindow;

class FolderSelect
{
public:
    explicit FolderSelect(MainWindow *window);

    void loadMusic(const QString &directory, bool remember);
    void createStartChoiceButtons(QVBoxLayout *content);
    void createStartContent(QVBoxLayout *layout);
    void connectStartPage();
    void createStartPage();
    void goHome();
    void goToRememberedFolder();
    QToolButton *createStartHomeButton();
    void collapseSearch(QLineEdit *edit);
    QToolButton *createSearchButton(QLineEdit *edit);
    QToolButton *createHomeButton();
    void restoreRememberedFolder();
    void openPath(const QString &path);
    void chooseFolder();
    void chooseFile();
    void loadRememberedFolderSetting();
    void saveRememberedFolder(const QString &directory);
    void clearRememberedFolder();

private:
    MainWindow *m_window = nullptr;
};

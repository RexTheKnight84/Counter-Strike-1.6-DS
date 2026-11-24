#pragma once

// The installation itself, off the window's thread: the game converted into
// this computer's cache, then what that made copied to the card. Maps a past
// run already converted, from the same game by the same program, are not done
// again.

#include <QString>
#include <QStringList>
#include <QThread>

#include <atomic>

#include "RomHeader.hpp"

class InstallWorker : public QThread
{
    Q_OBJECT
public:
    struct Job
    {
        QString game;     ///< the cstrike folder (Half-Life's valve beside it)
        QString card;     ///< the card's root
        QString cache;    ///< this computer's folder for the conversion
        QString rom;      ///< empty: none to copy
        QString ptexconv; ///< empty: look for it
        QStringList maps; ///< those to install
        bool textures = true;
        int threads = 0;  ///< threads the conversion keeps busy (ConvertOptions::jobs); 0: all
        QString language; ///< a translation beside the game (french); empty: the game's own
        QStringList expansions; ///< folders of the expansions to install too (bshift, gearbox): DSi only
        hlds::install::Device device = hlds::install::kDefaultDevice; ///< the console the ROM is made for
    };

    explicit InstallWorker(Job job, QObject *parent = nullptr) : QThread(parent), job_(std::move(job)) {}

    void stop() { stop_.store(true); }
    /// How many maps could not be converted, as the run ended.
    int failedMaps() const { return failed_; }

signals:
    void step(int step);                  ///< 0 checking, 1 converting, 2 making the ROM, 3 done
    void progress(int permille, const QString &now);
    void logLine(const QString &line);
    void finishedRun(bool ok, const QString &why);

protected:
    void run() override;

private:
    bool convert(QString &why);
    /// One game's maps (`game`: its folder) into the card's cache.
    bool convertGame(const QString &game, const QStringList &maps, QString &why);
    /// The converted game to the card's _nds/hl_nds/data, and `rom` to its
    /// root.
    bool copy(const QString &rom, QString &why);
    /// Counter-Strike 1.6 DS's ROM: the console's program, named and
    /// pictured as the game, none of the game in it (CsRom.hpp), in the
    /// cache (`made`).
    bool makeRom(QString &made, QString &why);

    Job job_;
    std::atomic<bool> stop_{false};
    int failed_ = 0;
};

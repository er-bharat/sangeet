# Sangeet
Music player for local files. Album & Playlist driven design listen to whole instead of hunting for 
next song so no mini player.  
Uses sql database creation and lazy loading so handles large local libraries excellently.  
Inspired by KDE's Elisa mp (not a fork), but uses libmpv instead of libvlc which is much more reliable.

strawberry has a feature called moodbar which i liked from aimp age but strawberry ui is dated, and
i wanted something modern looking so i implemented waveform progressbar in it.  
TBH this was the sole reason i wrote this new music player.  

## Screenshots
<img width="1199" height="797" alt="screenshot-20260928-121557" src="https://github.com/user-attachments/assets/492ed02e-bf7f-4c89-a3fb-47599fd3dfcf" />
<img width="1198" height="801" alt="screenshot-20260928-121544" src="https://github.com/user-attachments/assets/e30e9675-2737-4e94-bf45-a75af260defd" />



## Features
+ lazy load and sql db makes it near instant start even on first load.
+ can open file from file manager auto gets all the album songs to in same player view.
+ dropping a file or folder makes that folder the temp main source.
+ player have synced line lyrics function.
+ a waveform progressbar and normal progressbar.
+ shuffle ,repeat1 ,repeat all functions. revel by hover over album art
+ shows albums tracklist and playlist tracklist at bottom half of album art shown on hover over album art playing from tracks dosent list all tracks in it instead shows its albumTracks.
+ have MPRIS support so hardware and bluetooth device media buttons work.
+ have m3u/m3u8 playlists support also creation and editing tools.
+ through m3u playlists also support network streams added manually in m3u file.
+ tracks have multiple sorting method.

# FireMusic player (fmus)

Lightweight, simple TUI music player written in c++. Based on [cmus](https://cmus.github.io/) and [Rockbox](https://rockbox.org).

### Features include and are limited to:

>playing music organized into directories
>
>navigating through directories
>
>skiping tracks
>
>shuffling and looping
>
>and not much more

## Controls:
   >tab - settings menu

   >z/x - track skip

   >space - play/pause

   >s - toggle shuffle

   >r - toggle repeat

   >-/+ || scroll up/down - volume

   >arrow left/right - seek

   >arrow up/down - navigate

   >enter - select/play

   >shift +
   >
   >z/x - skip to start/end of playlist
   >
   >-/+ - lower incriments
   >
   >arrow left/right - higher incriments
   

## how to install:
> [!IMPORTANT]
> (script supports Debian and derivatives, RHEL/fedora, Arch and NixOS)
> 
> (only tested on arch btw)

1. clone repo:
   ```
   $ git clone https://github.com/Szczebrzeszyniec/fmus.git
   ```
3. cd into fmus directory:
   ```
   $ cd fmus
   ```
   
5. run script as root:
   ```
   $ sudo bash installer.sh
   ```
6. enjoy

otherwise install deps and build yourself:
>ncurses
>
>SDL
>
>SDL_mixer

and build (example, probably will work though):
   ```
   $ g++ main.cpp -std=c++17 -o fmus   `pkg-config --cflags --libs sdl2 SDL2_mixer ncurses`   -pthread
   ```

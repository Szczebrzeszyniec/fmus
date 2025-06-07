#include <ncursesw/ncurses.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <locale.h>
#include <chrono>
#include <thread>
#include <random>
#include <fstream>

using namespace std;
namespace fs = std::filesystem;

// settingsy
struct Settings {
    fs::path start_path;
    int repeat_mode_default; // 0=none,1=dir,2=one
    bool shuffle_default;
    int initial_volume_mode; // -1=Default(100%), 0=Keep Last, 100=Explicit(100%)
    int last_volume;
};
static Settings settings = {{}, 0, false, -1, 100};

// single RNG for shuffling, seeded once
static std::mt19937 rng{ std::random_device{}() };

// playback data lifted to globals so build_pl() can modify them
static vector<fs::path> playlist;
static vector<int>       order;
static int               cur = -1;

// declare here so main can call
void build_pl(const fs::path &f);

void load_settings() {
    ifstream in(string(getenv("HOME")) + "/.fmus-settings");
    if (!in) return;
    string line;
    while (getline(in, line)) {
        if (line.rfind("start_path=", 0) == 0)
            settings.start_path = line.substr(11);
        else if (line.rfind("repeat=", 0) == 0)
            settings.repeat_mode_default = stoi(line.substr(7));
        else if (line.rfind("shuffle=", 0) == 0)
            settings.shuffle_default = (line.substr(8) == "1");
        else if (line.rfind("init_vol_mode=", 0) == 0)
            settings.initial_volume_mode = stoi(line.substr(14));
        else if (line.rfind("last_vol=", 0) == 0)
            settings.last_volume = stoi(line.substr(9));
    }
}

void save_settings() {
    ofstream out(string(getenv("HOME")) + "/.fmus-settings");
    out << "start_path=" << settings.start_path.string() << "\n";
    out << "repeat=" << settings.repeat_mode_default << "\n";
    out << "shuffle=" << (settings.shuffle_default ? 1 : 0) << "\n";
    out << "init_vol_mode=" << settings.initial_volume_mode << "\n";
    out << "last_vol=" << settings.last_volume << "\n";
}

// reg help
vector<pair<string,string>> help_entries;
void register_help(const string &cmd, const string &desc) {
    help_entries.emplace_back(cmd, desc);
}

// modal help
static int rows, cols;
static void update_size() { getmaxyx(stdscr, rows, cols); }

void modal_help() {
    int ch;
    while (true) {
        update_size();
        clear();
        mvprintw(0,0,"Available Commands:");
        int y = 2;
        for (auto &e : help_entries) {
            mvprintw(y,2,"%s", e.first.c_str());
            mvprintw(y,16,"%s", e.second.c_str());
            ++y;
        }
        mvprintw(y+1,0,"Press Enter or Esc to return...");
        refresh();
        ch = getch();
        if (ch == 10 || ch == 27) break;
    }
}

// path edit modal
fs::path modal_path_edit(const fs::path &initial) {
    string buf = initial.string();
    int pos = buf.size(), ch;
    while (true) {
        update_size();
        clear();
        mvprintw(0,0,"Enter new start path (Esc to cancel):");
        mvprintw(1,0,"> %s", buf.c_str());
        move(1,2+pos);
        refresh();
        ch = getch();
        if (ch == 27) return initial;
        else if (ch == 10) {
            fs::path p(buf);
            return fs::exists(p) ? p : initial;
        }
        else if (ch == KEY_BACKSPACE || ch == 127) {
            if (pos>0) buf.erase(--pos,1);
        }
        else if (ch >=32 && ch<127) {
            buf.insert(buf.begin()+pos,(char)ch);
            ++pos;
        }
    }
}

// settings menu
void settings_menu() {
    vector<string> options;
    int sel=0, ch;
    auto refresh_opts = [&](){
        options = {
            "Start Path: " + settings.start_path.string(),
            string("Repeat Default: ") + (settings.repeat_mode_default==0? "None":
                                          settings.repeat_mode_default==1? "Dir":"One"),
            string("Shuffle Default: ") + (settings.shuffle_default? "On":"Off"),
            string("Initial Volume Mode: ") +
              (settings.initial_volume_mode<0? "Default(100%)":
               settings.initial_volume_mode==0? "Keep Last":"Explicit(100%)"),
            "Save & Return"
        };
    };
    refresh_opts();
    while (true) {
        update_size();
        clear();
        mvprintw(0,0,"Settings");
        for (int i=0;i<(int)options.size();++i) {
            if (i==sel) attron(A_REVERSE);
            mvprintw(i+2,2,"%s", options[i].c_str());
            if (i==sel) attroff(A_REVERSE);
        }
        ch = getch();
        int total = options.size();
        if (ch==KEY_UP)    sel = (sel-1+total)%total, refresh_opts();
        else if (ch==KEY_DOWN) sel = (sel+1)%total, refresh_opts();
        else if ((ch==KEY_LEFT||ch==KEY_RIGHT) && sel==3) {
            int &v = settings.initial_volume_mode;
            if (ch==KEY_RIGHT) v = (v<0?0: v==0?100:-1);
            else              v = (v<0?100: v==0?-1:0);
        }
        else if (ch==10) {
            switch(sel){
              case 0: settings.start_path = modal_path_edit(settings.start_path); break;
              case 1: settings.repeat_mode_default = (settings.repeat_mode_default+1)%3; break;
              case 2: settings.shuffle_default = !settings.shuffle_default; break;
              case 3: /* already toggled above */ break;
              case 4: save_settings(); return;
            }
            refresh_opts();
        }
        else if (ch==27) { save_settings(); return; }
    }
}

// file list
vector<fs::path> list_items(const fs::path &dir) {
    vector<fs::path> v;
    static const vector<string> exts = {
      ".mp3",".wav",".flac",".ogg",".aac",
      ".m4a",".wma",".alac",".aiff",".opus"
    };
    for (auto &e: fs::directory_iterator(dir)) {
        if (e.is_directory()) v.push_back(e.path());
        else {
            string ext = e.path().extension().string();
            transform(ext.begin(),ext.end(),ext.begin(),::tolower);
            if (find(exts.begin(),exts.end(),ext)!=exts.end())
                v.push_back(e.path());
        }
    }
    sort(v.begin(),v.end(),[](auto &a,auto &b){
        bool da=fs::is_directory(a), db=fs::is_directory(b);
        if (da!=db) return da>db;
        return a.filename().wstring()<b.filename().wstring();
    });
    return v;
}

string fmt_time(int s) {
    int h=s/3600, m=(s%3600)/60, r=s%60;
    char buf[16];
    if (h>0) snprintf(buf,sizeof(buf),"%d:%02d:%02d",h,m,r);
    else     snprintf(buf,sizeof(buf),"%02d:%02d",m,r);
    return string(buf);
}

// playback call
static bool done_cb = false;
static void music_done() { done_cb = true; }


int main() {
    setlocale(LC_ALL,"");
    load_settings();
    register_help(":help","Show help");
    register_help(":path <dir>","Change start path");
    register_help("TAB / :s","Open settings menu");
    register_help(":settings","Open settings menu");
    register_help(":q / :quit","Exit player");
    // register_help("-- Controls --","");
    // register_help("z/x", "prev / next track, shift to skip to list start / end");
    // register_help("space","pause/play");
    // register_help("+/-","volume, shift for lower incriments");
    // register_help("scroll wheel","volume");
    // register_help("arrow right/left keys","scrub, shift for higher incriments");





    

    initscr(); cbreak(); noecho(); keypad(stdscr,TRUE);
    curs_set(0); timeout(100); mousemask(ALL_MOUSE_EVENTS,nullptr);

    SDL_Init(SDL_INIT_AUDIO);
    Mix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,2048);
    Mix_HookMusicFinished(music_done);

    fs::path cwd = settings.start_path.empty()
                      ? fs::path(getenv("HOME"))
                      : settings.start_path;
    auto items = list_items(cwd);

    int sel = 0, off = 0;
    Mix_Music *music = nullptr;
    bool playing = false;
    wstring cur_name;
    auto start_t = chrono::steady_clock::now();
    int track_len = 0, volume = 100;
    if (settings.initial_volume_mode==0)      volume = settings.last_volume;
    else if (settings.initial_volume_mode>0) volume = settings.initial_volume_mode;
    Mix_VolumeMusic(volume*MIX_MAX_VOLUME/100);

    bool cmd = false;
    string cmdbuf;
    update_size();

    auto set_time = [&](double p){
        using sc = chrono::steady_clock;
        auto d = chrono::duration_cast<sc::duration>(chrono::duration<double>(p));
        start_t = sc::now() - d;
    };
    auto playidx = [&](int i){
        if (music) { Mix_HaltMusic(); Mix_FreeMusic(music); }
        if (i<0 || i>=(int)order.size()) return;
        music = Mix_LoadMUS(playlist[order[i]].string().c_str());
        Mix_PlayMusic(music,1);
        playing = true;
        cur_name = playlist[order[i]].filename().wstring();
        start_t = chrono::steady_clock::now();
        track_len = int(Mix_MusicDuration(music));
        cur = i;
    };
    auto next = [&](){
        if (cur<0 || order.empty()) return;
        if (settings.repeat_mode_default==2) return playidx(cur);
        int n = cur+1;
        if (n >= (int)order.size()) {
            if (settings.repeat_mode_default==1) n = 0;
            else { playing=false; Mix_HaltMusic(); return; }
        }
        playidx(n);
    };
    auto prev = [&](){
        if (cur<0 || order.empty()) return;
        if (settings.repeat_mode_default==2) return playidx(cur);
        int p = cur-1;
        if (p<0) {
            if (settings.repeat_mode_default==1) p = order.size()-1;
            else return;
        }
        playidx(p);
    };

    auto draw = [&]() {
        update_size();
        clear();
    
        //build list display
        vector<fs::path> disp;
        disp.push_back("^");
        disp.insert(disp.end(), items.begin(), items.end());
        int total = disp.size();
        int vh = rows - 3;
        if (sel < off) off = sel;
        if (sel >= off + vh) off = sel - vh + 1;
    
        //what playing
        fs::path nowp;
        if (music && cur >= 0) nowp = playlist[order[cur]];
    
        //draw list
        for (int i = 0; i < vh && i + off < total; ++i) {
            int idx = i + off;
            bool highlighted = (idx == sel),
                 is_now      = (idx > 0 && music && disp[idx] == nowp);
    
            if (highlighted && is_now) {
                attron(A_BOLD);
                mvprintw(i+1, 0, "!>  ");
                attroff(A_BOLD);
            }
            else if (highlighted) {
                mvprintw(i+1, 0, " >  ");
            }
            else if (is_now) {
                attron(A_DIM);
                mvprintw(i+1, 0, "!-  ");
                attroff(A_DIM);
            }
            else {
                mvprintw(i+1, 0, "    ");
            }
    
            string name = (idx == 0)
                            ? "/^/"
                            : "/" + disp[idx].filename().string()
                              + (fs::is_directory(disp[idx]) ? "/" : "");
            mvprintw(i+1, 4, "%s", name.c_str());
        }
    
        //draw bar
        if (music) {
            //elapsed time
            double elapsed = chrono::duration_cast<chrono::duration<double>>(
                chrono::steady_clock::now() - start_t).count();
            int ie = min(track_len, int(elapsed));
            string cur_t = fmt_time(ie),
                   tot_t = fmt_time(track_len);
    
            //shuffle/repeat
            string mode = "[" +
                string(settings.shuffle_default ? "S" : "-") + "|" +
                (settings.repeat_mode_default == 0 ? "N"
                 : settings.repeat_mode_default == 1 ? "D" : "O")
                + "]";
    
            //Bstatus text
            string status = cur_t + "/" + tot_t + " " + mode;
            if (!playing) status += " [pause]";
    
            // rim
            vector<char> buf(cur_name.size()*4 + 1);
            wcstombs(buf.data(), cur_name.c_str(), buf.size());
            string tname(buf.data());
            int w = status.size();
            int av = cols - w - 1;
            if ((int)tname.size() > av)
                tname = tname.substr(0, av - 3) + "...";
    

            mvprintw(rows - 2, 0, "%s", tname.c_str());
            mvprintw(rows - 2, cols - w, "%s", status.c_str());
    
            //last row
            mvprintw(rows - 1, 0, "Vol: %d%%", volume);
        }
    
        refresh();
    };
    

    draw();
    while (true) {
        int c = getch();
        MEVENT me;

        //resize
        if (c == KEY_RESIZE) {
            draw();
            continue;
        }

        //Ccmd
        if (cmd) {
            if (c == 10) {
                if (cmdbuf == "help") {
                    modal_help();
                }
                else if (cmdbuf.rfind("path", 0) == 0) {
                    string argstr = cmdbuf.size() > 5 ? cmdbuf.substr(5) : "";
                    if (!argstr.empty()) {
                        fs::path arg(argstr);
                        if (fs::exists(arg) && fs::is_directory(arg))
                            settings.start_path = arg;
                        else {
                            mvprintw(rows-1,0,"Invalid directory: %s",argstr.c_str());
                            clrtoeol(); getch();
                        }
                    } else {
                        settings.start_path = modal_path_edit(settings.start_path);
                    }
                    cwd = settings.start_path;
                    items = list_items(cwd);
                    sel = off = 0;
                }
                else if (cmdbuf=="settings"||cmdbuf=="s") {
                    settings_menu();
                }
                else if (cmdbuf=="quit"||cmdbuf=="q") {
                    break;
                }
                cmd = false;
                cmdbuf.clear();
                draw();
            }
            else if (c == 27) {
                cmd = false;
                cmdbuf.clear();
                draw();
            }
    
            else if (c >= 32 && c < 127) {
                cmdbuf.push_back((char)c);
                mvprintw(rows-1,1,"%s",cmdbuf.c_str());
                refresh();
            }
            continue;
        }

        //start cmd
        if (c == ':') {
            cmd = true;
            cmdbuf.clear();
            mvprintw(rows-1,0,":");
            clrtoeol();
            refresh();
            continue;
        }

        //mouse wheel for volume (5%)
        if (c == KEY_MOUSE && getmouse(&me) == OK) {
            if (me.bstate & BUTTON4_PRESSED) volume = min(200, volume + 5);
            if (me.bstate & BUTTON5_PRESSED) volume = max(0,   volume - 5);
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
        }
                //toggle into settings
                else if (c == '\t') {      // Tab key
             settings_menu();
                }
        
        //volume keys
        else if (c == '=') {               //unshifted 5%
            volume = min(100, volume + 5);
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
        }
        else if (c == '+') {               //shifted 1%
            volume = min(100, volume + 1);
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
        }
        else if (c == '-') {               //unshifted 5%
            volume = max(0, volume - 5);
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
        }
        else if (c == '_') {               //shifted 1%
            volume = max(0, volume - 1);
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
        }
        // wrap
        else if (c == KEY_UP) {
            int total = items.size() + 1;
            sel = (sel - 1 + total) % total;
            draw();
        }
        else if (c == KEY_DOWN) {
            int total = items.size() + 1;
            sel = (sel + 1) % total;
            draw();
        }
        else if (c == 10) {
            if (sel == 0) {
                if (cwd.has_parent_path()) cwd = cwd.parent_path();
                items = list_items(cwd);
                sel = off = 0;
            } else {
                fs::path t = items[sel - 1];
                if (fs::is_directory(t)) {
                    cwd = t;
                    items = list_items(cwd);
                    sel = off = 0;
                } else {
                    build_pl(t);
                    playidx(cur);
                }
            }
            draw();
        }


        


        //pause/resume
     else if (c == ' ') {
            if (music) {
                if (playing) {
                    Mix_PauseMusic();
                    playing = false;
                } else {
                    Mix_ResumeMusic();
                    playing = true;
                    set_time(Mix_GetMusicPosition(music));
                }
                draw();
            }
        }
        //prev / next
        else if (c == 'z') {
            prev(); draw();
        }
        else if (c == 'x') {
            next(); draw();
        }
        // Shift+Z / Shift+X
        else if (c == 'Z') {
            if (!order.empty()) playidx(0);
            draw();
        }
        else if (c == 'X') {
            if (!order.empty()) playidx(order.size() - 1);
            draw();
        }
        //scrub
        else if (c == KEY_LEFT && music) {
            double p = Mix_GetMusicPosition(music) - 1.0;
            if (p < 0) p = 0;
            Mix_SetMusicPosition(p);
            set_time(p);
            draw();
        }
        else if (c == KEY_RIGHT && music) {
            double p = Mix_GetMusicPosition(music) + 1.0;
            if (p > track_len) p = track_len;
            Mix_SetMusicPosition(p);
            set_time(p);
            draw();
        }
        else if (c == KEY_SLEFT && music) {
            double p = Mix_GetMusicPosition(music) - 5.0;
            if (p < 0) p = 0;
            Mix_SetMusicPosition(p);
            set_time(p);
            draw();
        }
        else if (c == KEY_SRIGHT && music) {
            double p = Mix_GetMusicPosition(music) + 5.0;
            if (p > track_len) p = track_len;
            Mix_SetMusicPosition(p);
            set_time(p);
            draw();
        }
        //shuffle
        else if (c == 's') {
            settings.shuffle_default = !settings.shuffle_default;
            if (music && cur >= 0 && cur < (int)order.size()) {
                fs::path now = playlist[order[cur]];
                build_pl(now);
            }
            draw();
        }
        //repeat
        else if (c == 'r') {
            settings.repeat_mode_default = (settings.repeat_mode_default + 1) % 3;
            draw();
        }
        //quit ctrl c
        else if (c == 3) {
            break;
        }

        // play next on end
        if (done_cb) {
            done_cb = false;
            if (!Mix_PlayingMusic()) {
                next();
                draw();
            }
        }

        //update
        if (playing && music) {
            draw();
        }

        //sleep
        this_thread::sleep_for(chrono::milliseconds(10));
    }


    if (music) Mix_FreeMusic(music);
    Mix_CloseAudio();
    endwin();
    SDL_Quit();
    save_settings();
    return 0;
}

//playlista
void build_pl(const fs::path &f)
{
    playlist.clear();
    order.clear();
    cur = -1;

    auto parent = f.parent_path();
    if (!fs::exists(parent) || !fs::is_directory(parent))
        return;

    static const vector<string> audio_ext = {
        ".mp3", ".wav",  ".flac", ".ogg",  ".aac",
        ".m4a", ".wma",  ".alac", ".aiff", ".opus"
    };

    //dir only
    for (auto &e : fs::directory_iterator(parent))
    {
        if (!e.is_directory())
        {
            string ext = e.path().extension().string();
            transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (find(audio_ext.begin(), audio_ext.end(), ext) != audio_ext.end())
                playlist.push_back(e.path());
        }
    }

    //sort auto
    sort(playlist.begin(), playlist.end(),
         [](auto &a, auto &b) {
             return a.filename().wstring() < b.filename().wstring();
         });

    // uild/shuffle
    order.resize(playlist.size());
    iota(order.begin(), order.end(), 0);
    if (settings.shuffle_default && order.size() > 1)
        shuffle(order.begin(), order.end(), rng);

    //index
    for (int i = 0; i < (int)playlist.size(); ++i)
    {
        if (playlist[i] == f)
        {
            for (int j = 0; j < (int)order.size(); ++j)
            {
                if (order[j] == i)
                {
                    cur = j;
                    return;
                }
            }
        }
    }
}

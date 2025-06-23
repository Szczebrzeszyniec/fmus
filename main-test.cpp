#include <ncurses.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <locale.h>
#include <chrono>
#include <thread>
#include <random>
#include <fstream>
#include <sdbus-c++/sdbus-c++.h>

using namespace std;
namespace fs = std::filesystem;

// ---------------- Settings struct & load/save ----------------

struct Settings {
    fs::path start_path;
    int repeat_mode_default;    // 0=none,1=dir,2=one
    bool shuffle_default;
    int initial_volume_mode;    // -1=Default,0=Keep Last,>0 explicit
    int last_volume;
    bool reshuffle_on_end;
    string icon_dirup;
    string icon_nowplaying;
    string icon_nowplaying_sel;
};
static Settings settings = {
    {},0,false,-1,100,false,"/^/","!-","!>"
};
static mt19937 rng{ random_device{}() };

void load_settings() {
    ifstream in(string(getenv("HOME")) + "/.fmus-settings");
    if (!in) return;
    string line;
    while (getline(in, line)) {
        auto pos = line.find('=');
        if (pos == string::npos) continue;
        string key = line.substr(0,pos), val = line.substr(pos+1);
        if (key=="start_path")           settings.start_path = val;
        else if (key=="repeat")          settings.repeat_mode_default = stoi(val);
        else if (key=="shuffle")         settings.shuffle_default = (val=="1");
        else if (key=="init_vol_mode")   settings.initial_volume_mode = stoi(val);
        else if (key=="last_vol")        settings.last_volume = stoi(val);
        else if (key=="reshuffle")       settings.reshuffle_on_end = (val=="1");
        else if (key=="icon_dirup")      settings.icon_dirup = val;
        else if (key=="icon_nowplaying") settings.icon_nowplaying = val;
        else if (key=="icon_nowplaying_sel") settings.icon_nowplaying_sel = val;
    }
}
void save_settings() {
    ofstream out(string(getenv("HOME")) + "/.fmus-settings");
    out<<"start_path="<<settings.start_path.string()<<"\n";
    out<<"repeat="<<settings.repeat_mode_default<<"\n";
    out<<"shuffle="<<(settings.shuffle_default?1:0)<<"\n";
    out<<"init_vol_mode="<<settings.initial_volume_mode<<"\n";
    out<<"last_vol="<<settings.last_volume<<"\n";
    out<<"reshuffle="<<(settings.reshuffle_on_end?1:0)<<"\n";
    out<<"icon_dirup="<<settings.icon_dirup<<"\n";
    out<<"icon_nowplaying="<<settings.icon_nowplaying<<"\n";
    out<<"icon_nowplaying_sel="<<settings.icon_nowplaying_sel<<"\n";
}

// ---------------- ncurses help & UI ----------------

vector<pair<string,string>> help_entries;
void register_help(const string &c,const string &d){ help_entries.emplace_back(c,d); }
static int rows, cols;
static void update_size(){ getmaxyx(stdscr, rows, cols); }

void modal_help(){
    int ch;
    while(true){
        update_size(); clear();
        mvprintw(0,0,"Available Commands:");
        int y=2;
        for(auto &e:help_entries){
            mvprintw(y,2,"%s",e.first.c_str());
            mvprintw(y,16,"%s",e.second.c_str());
            ++y;
        }
        mvprintw(y+1,0,"Press Enter or Esc to return...");
        refresh();
        ch=getch();
        if(ch==10||ch==27) break;
    }
}

fs::path modal_path_edit(const fs::path &initial){
    string buf = initial.string();
    int pos = buf.size(), ch;
    while(true){
        update_size(); clear();
        mvprintw(0,0,"Enter new start path (Esc to cancel):");
        mvprintw(1,0,"> %s",buf.c_str());
        move(1,2+pos); refresh();
        ch=getch();
        if(ch==27) return initial;
        else if(ch==10){
            fs::path p(buf);
            return fs::exists(p)?p:initial;
        }
        else if(ch==KEY_BACKSPACE||ch==127){
            if(pos>0) buf.erase(--pos,1);
        }
        else if(ch>=32&&ch<127){
            buf.insert(buf.begin()+pos,(char)ch);
            ++pos;
        }
    }
}

string modal_text_edit(const string &prompt, const string &initial){
    string buf = initial;
    int pos = buf.size(), ch;
    while(true){
        update_size(); clear();
        mvprintw(0,0,"%s (Esc to cancel):",prompt.c_str());
        mvprintw(1,0,"> %s",buf.c_str());
        move(1,2+pos); refresh();
        ch=getch();
        if(ch==27) return initial;
        else if(ch==10) return buf;
        else if(ch==KEY_BACKSPACE||ch==127){
            if(pos>0) buf.erase(--pos,1);
        }
        else if(ch>=32&&ch<127){
            buf.insert(buf.begin()+pos,(char)ch);
            ++pos;
        }
    }
}

bool settings_menu(){
    vector<string> opts; int sel=0,ch;
    auto refresh_opts=[&](){
        opts = {
            "Start Path: " + settings.start_path.string(),
            string("Repeat Default: ")+
              (settings.repeat_mode_default==0?"None":
               settings.repeat_mode_default==1?"Dir":"One"),
            string("Shuffle Default: ")+(settings.shuffle_default?"On":"Off"),
            string("Reshuffle On End: ")+(settings.reshuffle_on_end?"On":"Off"),
            "Icon DirUp: "+settings.icon_dirup,
            "Icon NowPlaying: "+settings.icon_nowplaying,
            "Icon NowPlaySel: "+settings.icon_nowplaying_sel,
            "Save & Return","Quit"
        };
    };
    refresh_opts();
    while(true){
        update_size(); clear();
        mvprintw(0,0,"Settings");
        for(int i=0;i<opts.size();++i){
            if(i==sel) attron(A_REVERSE);
            mvprintw(i+2,2,"%s",opts[i].c_str());
            if(i==sel) attroff(A_REVERSE);
        }
        refresh(); ch=getch();
        int n=opts.size();
        if(ch==KEY_UP)    sel=(sel-1+n)%n, refresh_opts();
        else if(ch==KEY_DOWN) sel=(sel+1)%n, refresh_opts();
        else if(ch==10){
            switch(sel){
            case 0: settings.start_path = modal_path_edit(settings.start_path); break;
            case 1: settings.repeat_mode_default=(settings.repeat_mode_default+1)%3; break;
            case 2: settings.shuffle_default=!settings.shuffle_default; break;
            case 3: settings.reshuffle_on_end=!settings.reshuffle_on_end; break;
            case 4: settings.icon_dirup=modal_text_edit("New Dir-Up Icon",settings.icon_dirup); break;
            case 5: settings.icon_nowplaying=modal_text_edit("New NowPlaying Icon",settings.icon_nowplaying); break;
            case 6: settings.icon_nowplaying_sel=modal_text_edit("New NowPlaySel Icon",settings.icon_nowplaying_sel); break;
            case 7: save_settings(); return false;
            case 8: save_settings(); return true;
            }
            refresh_opts();
        }
        else if(ch==27){ save_settings(); return false; }
    }
}

// ---------------- playlist helpers ----------------

vector<fs::path> list_items(const fs::path &dir){
    vector<fs::path> v;
    static const vector<string> exts={".mp3",".wav",".flac",".ogg",".aac",".m4a",".wma",".alac",".aiff",".opus"};
    for(auto &e:fs::directory_iterator(dir)){
        if(e.is_directory()) v.push_back(e.path());
        else {
            string ext=e.path().extension().string();
            transform(ext.begin(),ext.end(),ext.begin(),::tolower);
            if(find(exts.begin(),exts.end(),ext)!=exts.end())
                v.push_back(e.path());
        }
    }
    sort(v.begin(),v.end(),[](auto&a,auto&b){
        bool da=fs::is_directory(a), db=fs::is_directory(b);
        if(da!=db) return da>db;
        return a.filename().wstring()<b.filename().wstring();
    });
    return v;
}

string fmt_time(int s){
    int h=s/3600, m=(s%3600)/60, r=s%60;
    char buf[16];
    if(h>0) snprintf(buf,sizeof(buf),"%d:%02d:%02d",h,m,r);
    else    snprintf(buf,sizeof(buf),"%02d:%02d",m,r);
    return string(buf);
}

// ---------------- playback globals ----------------

static vector<fs::path> playlist;
static vector<int>      order;
static int              cur = -1;
static bool             done_cb = false;
static void music_done(){ done_cb=true; }

// ---------------- main() ----------------

int main(){
    // 1) Locale & settings
    setlocale(LC_ALL,"");
    load_settings();
    register_help(":help","Show help");
    register_help(":settings","Open settings");
    register_help(":q","Quit");

    // 2) ncurses init
    initscr(); cbreak(); noecho(); keypad(stdscr,TRUE);
    curs_set(0); timeout(10); mousemask(ALL_MOUSE_EVENTS,nullptr);

    // 3) SDL audio init
    SDL_Init(SDL_INIT_AUDIO);
    Mix_OpenAudio(44100,MIX_DEFAULT_FORMAT,2,2048);
    Mix_HookMusicFinished(music_done);

    // 4) MPRIS setup (run once, *before* the loop)
    auto connection = sdbus::createSessionBusConnection();
    connection->requestName( sdbus::BusName{"org.mpris.MediaPlayer2.fmus"} );
    sdbus::ObjectPath mprisPath{ "/org/mpris/MediaPlayer2" };
    auto object = sdbus::createObject(*connection, mprisPath);

 // 4) MPRIS setup (run once, *before* the loop)
auto connection = sdbus::createSessionBusConnection();
connection->requestName( sdbus::BusName{"org.mpris.MediaPlayer2.fmus"} );
sdbus::ObjectPath mprisPath{ "/org/mpris/MediaPlayer2" };
auto object = sdbus::createObject(*connection, mprisPath);

// 4a) Core interface
auto ifaceCore = object->registerInterface("org.mpris.MediaPlayer2");
ifaceCore->registerProperty("CanQuit")
         .onInterface("org.mpris.MediaPlayer2")
         .withGetter([]{ return false; });
ifaceCore->registerProperty("CanRaise")
         .onInterface("org.mpris.MediaPlayer2")
         .withGetter([]{ return false; });
ifaceCore->registerProperty("HasTrackList")
         .onInterface("org.mpris.MediaPlayer2")
         .withGetter([]{ return false; });
ifaceCore->registerProperty("Identity")
         .onInterface("org.mpris.MediaPlayer2")
         .withGetter([]{ return string("FMus"); });
ifaceCore->registerProperty("DesktopEntry")
         .onInterface("org.mpris.MediaPlayer2")
         .withGetter([]{ return string("fmus"); });
ifaceCore->finishRegistration();

// 4b) Player interface
auto ifacePlayer = object->registerInterface("org.mpris.MediaPlayer2.Player");
ifacePlayer->registerProperty("PlaybackStatus")
           .onInterface("org.mpris.MediaPlayer2.Player")
           .withGetter([&](){ return playing ? string("Playing") : string("Paused"); });
ifacePlayer->registerProperty("Metadata")
           .onInterface("org.mpris.MediaPlayer2.Player")
           .withGetter([&](){
               map<string, sdbus::Variant> md;
               if (music && cur >= 0) {
                   vector<char> buf(cur_name.size()*4+1);
                   wcstombs(buf.data(), cur_name.c_str(), buf.size());
                   md["xesam:title"] = sdbus::Variant{ string(buf.data()) };
               }
               return md;
           });
ifacePlayer->registerMethod("Play")
           .onInterface("org.mpris.MediaPlayer2.Player")
           .implementedAs([&](){ /* … */ });
ifacePlayer->registerMethod("Pause")
           .onInterface("org.mpris.MediaPlayer2.Player")
           .implementedAs([&](){ /* … */ });
ifacePlayer->registerMethod("Next")
           .onInterface("org.mpris.MediaPlayer2.Player")
           .implementedAs(nextFwd);
ifacePlayer->registerMethod("Previous")
           .onInterface("org.mpris.MediaPlayer2.Player")
           .implementedAs(prevFwd);
ifacePlayer->finishRegistration();


    // forwarders
    function<void()> nextFwd, prevFwd;
    auto emitPlayChange = [&](){
        ifacePlayer->emitPropertyChanged("PlaybackStatus");
        ifacePlayer->emitPropertyChanged("Metadata");
    };

    auto playidx = [&](int i){
        if(music){ Mix_HaltMusic(); Mix_FreeMusic(music); }
        if(i<0||i>=(int)order.size()) return;
        music = Mix_LoadMUS(playlist[order[i]].string().c_str());
        Mix_PlayMusic(music,1);
        playing = true;
        cur_name = playlist[order[i]].filename().wstring();
        track_len = int(Mix_MusicDuration(music));
        cur = i;
        emitPlayChange();
    };
    nextFwd = [&](){
        if(cur<0||order.empty()) return;
        if(settings.repeat_mode_default==2){ playidx(cur); return; }
        int n=cur+1;
        if(n>=(int)order.size()){
            if(settings.reshuffle_on_end){
                shuffle(order.begin(),order.end(),rng);
                playidx(0);
            }
            else if(settings.repeat_mode_default==1) playidx(0);
            else { playing=false; Mix_HaltMusic(); emitPlayChange(); }
        } else playidx(n);
    };
    prevFwd = [&](){
        if(cur<0||order.empty()) return;
        if(settings.repeat_mode_default==2){ playidx(cur); return; }
        int p=cur-1;
        if(p<0){
            if(settings.repeat_mode_default==1) p=order.size()-1;
            else return;
        }
        playidx(p);
    };

    ifacePlayer->registerMethod("Play")
               .onInterface("org.mpris.MediaPlayer2.Player")
               .implementedAs([&](){
                   if(!playing && music){
                       Mix_ResumeMusic();
                       playing=true;
                       set_time(Mix_GetMusicPosition(music));
                       emitPlayChange();
                   }
               });
    ifacePlayer->registerMethod("Pause")
               .onInterface("org.mpris.MediaPlayer2.Player")
               .implementedAs([&](){
                   if(playing && music){
                       Mix_PauseMusic();
                       playing=false;
                       emitPlayChange();
                   }
               });
    ifacePlayer->registerMethod("Next")
               .onInterface("org.mpris.MediaPlayer2.Player")
               .implementedAs(nextFwd);
    ifacePlayer->registerMethod("Previous")
               .onInterface("org.mpris.MediaPlayer2.Player")
               .implementedAs(prevFwd);
    ifacePlayer->finishRegistration();

    // 5) Determine start directory & UI state
    fs::path cwd = settings.start_path.empty() ? fs::path(getenv("HOME")) : settings.start_path;
    auto items = list_items(cwd);
    int sel=0, off=0;
    int volume=100;
    if(settings.initial_volume_mode==0)      volume = settings.last_volume;
    else if(settings.initial_volume_mode>0) volume = settings.initial_volume_mode;
    Mix_VolumeMusic(volume*MIX_MAX_VOLUME/100);
    bool cmd=false; string cmdbuf;
    update_size();

    auto build_pl = [&](const fs::path &f){
        playlist.clear(); order.clear(); cur=-1;
        fs::path parent = f.parent_path();
        if(!fs::exists(parent)||!fs::is_directory(parent)) return;
        static const vector<string> exts={".mp3",".wav",".flac",".ogg",".aac",".m4a",".wma"
                                         ,".alac",".aiff",".opus"};
        for(auto &e: fs::directory_iterator(parent)){
            if(!e.is_directory()){
                string ext = e.path().extension().string();
                transform(ext.begin(),ext.end(),ext.begin(),::tolower);
                if(find(exts.begin(),exts.end(),ext)!=exts.end())
                    playlist.push_back(e.path());
            }
        }
        sort(playlist.begin(),playlist.end(),[](auto &a,auto &b){
            return a.filename().wstring()<b.filename().wstring();
        });
        order.resize(playlist.size());
        iota(order.begin(),order.end(),0);
        if(settings.shuffle_default && order.size()>1)
            shuffle(order.begin(),order.end(),rng);
        for(int i=0;i<(int)playlist.size();++i){
            if(playlist[i]==f){
                for(int j=0;j<(int)order.size();++j){
                    if(order[j]==i){ cur=j; return; }
                }
            }
        }
    };

    auto draw = [&](){
        update_size();
        clear();
        int total = items.size()+1, vh = rows-4;
        if(sel<off) off = sel;
        if(sel>=off+vh) off = sel-vh+1;
        fs::path nowp;
        if(music && cur>=0) nowp = playlist[order[cur]];
        for(int i=0;i<vh && i+off<total;++i){
            int idx = i+off;
            bool hl = (idx==sel);
            const char* icon; string name;
            if(idx==0){
                icon = hl ? " > " : "   ";
                name = settings.icon_dirup;
            } else {
                fs::path p = items[idx-1];
                bool isNow = (p==nowp);
                if(isNow) icon = hl? settings.icon_nowplaying_sel.c_str() : settings.icon_nowplaying.c_str();
                else      icon = hl? " > " : "   ";
                name = p.filename().string() + (fs::is_directory(p)?"/":"");
            }
            mvprintw(i+1,0,"%s  ",icon);
            mvprintw(i+1,5,"%s",name.c_str());
            if(idx>0){
                fs::path p = items[idx-1];
                auto it = find_if(order.begin(),order.end(),[&](int o){ return playlist[o]==p; });
                if(it!=order.end()){
                    int pos = int(it-order.begin());
                    string ind = "["+to_string(pos+1)+"/"+to_string(order.size())+"]";
                    mvprintw(i+1,cols-ind.size(),"%s",ind.c_str());
                }
            }
        }

        if(music){
            static chrono::steady_clock::time_point start_t; // reused by set_time
            double elapsed = playing
                ? chrono::duration_cast<chrono::duration<double>>(chrono::steady_clock::now()-start_t).count()
                : Mix_GetMusicPosition(music);
            int ie = min(track_len,int(elapsed));
            int fill = cols ? int((double)ie/track_len*cols+0.5) : 0;
            int ybar = rows-3;
            for(int x=0;x<cols;++x) mvaddch(ybar,x, x<fill?ACS_CKBOARD:' ');

            string cur_t=fmt_time(ie), tot_t=fmt_time(track_len);
            string mode = "[" + string(settings.shuffle_default?"S":"-") + "|" +
                          (settings.repeat_mode_default==0?"N":
                           settings.repeat_mode_default==1?"D":"O") + "]";
            string status = cur_t+"/"+tot_t+" "+mode + (playing?"":" [pause]");

            vector<char> buf(cur_name.size()*4+1);
            wcstombs(buf.data(), cur_name.c_str(), buf.size());
            string tname(buf.data());
            int w=status.size(), av=cols-w-1;
            if((int)tname.size()>av) tname=tname.substr(0,av-3)+"...";
            mvprintw(rows-2,0,"%s",tname.c_str());
            mvprintw(rows-2,cols-w,"%s",status.c_str());
            mvprintw(rows-1,0,"Vol: %d%%",volume);
        }

        refresh();
    };

    draw();

    while (true) {
        // 1) Service any incoming MPRIS calls
        connection->processPendingEvent();

        // 2) Get one key/mouse event
        int c = getch();
        MEVENT me;

        // 3) ESC → settings menu
        if (!cmd && c == 27) {
            timeout(-1);
            if (settings_menu()) break;       // quit if settings_menu returns true
            update_size(); clear(); refresh(); // otherwise restore UI
            timeout(10);
            draw();
            continue;
        }

        // 4) Terminal resize → redraw
        if (c == KEY_RESIZE) {
            draw();
            continue;
        }

        // 5) “Command-mode” input (after you pressed ‘:’)
        if (cmd) {
            if (c == 10) {
                if      (cmdbuf == "help")    modal_help();
                else if (cmdbuf == "quit" || cmdbuf == "q") break;
                else if (cmdbuf == "settings" || cmdbuf == "s") settings_menu();
                cmd = false; cmdbuf.clear(); draw();
            }
            else if (c == 27) { cmd = false; cmdbuf.clear(); draw(); }
            else if (c >= 32 && c < 127) {
                cmdbuf.push_back((char)c);
                mvprintw(rows-1,1,"%s",cmdbuf.c_str());
                refresh();
            }
            continue;
        }
        if (c == ':') {
            cmd = true; cmdbuf.clear();
            mvprintw(rows-1,0,":"); clrtoeol(); refresh();
            continue;
        }

        // 6) Volume via mouse wheel
        if (c == KEY_MOUSE && getmouse(&me) == OK) {
            if (me.bstate & BUTTON4_PRESSED) volume = min(200, volume + 5);
            if (me.bstate & BUTTON5_PRESSED) volume = max(0,   volume - 5);
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
            continue;
        }

        // 7) Volume via +/-/_/= keys
        if      (c == '=') volume = min(100, volume + 5);
        else if (c == '+') volume = min(100, volume + 1);
        else if (c == '-') volume = max(0,   volume - 5);
        else if (c == '_') volume = max(0,   volume - 1);
        if (c=='='||c=='+'||c=='-'||c=='_') {
            Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
            settings.last_volume = volume;
            draw();
            continue;
        }

        // 8) Up/down to move selection
        if      (c == KEY_UP)   { sel = (sel - 1 + items.size() + 1) % (items.size() + 1); draw(); }
        else if (c == KEY_DOWN) { sel = (sel + 1) % (items.size() + 1); draw(); }

        // 9) Enter/Select
        else if (c == 10) {
            if (sel == 0) {
                if (cwd.has_parent_path()) cwd = cwd.parent_path();
                items = list_items(cwd);
                sel = off = 0;
            } else {
                fs::path t = items[sel - 1];
                if (fs::is_directory(t)) {
                    cwd = t; items = list_items(cwd);
                    sel = off = 0;
                } else {
                    build_pl(t);
                    playidx(cur);
                    emitPlayChange();
                }
            }
            draw();
        }

        // 10) Space → play/pause
        else if (c == ' ' && music) {
            if (playing) {
                Mix_PauseMusic();
                playing = false;
            } else {
                Mix_ResumeMusic();
                playing = true;
                set_time(Mix_GetMusicPosition(music));
            }
            emitPlayChange();
            draw();
        }

        // 11) Previous / Next track
        else if (c == 'z') { prevFwd(); emitPlayChange(); draw(); }
        else if (c == 'x') { nextFwd(); emitPlayChange(); draw(); }
        else if (c == 'Z') { if (!order.empty()) { playidx(0); emitPlayChange(); } draw(); }
        else if (c == 'X') { if (!order.empty()) { playidx(order.size()-1); emitPlayChange(); } draw(); }

        // 12) Seek backward/forward
        else if ((c == KEY_LEFT || c == KEY_SLEFT) && music) {
            double d = (c == KEY_LEFT ? 1 : 5);
            double p = Mix_GetMusicPosition(music) - d;
            p = max(0.0, p);
            Mix_SetMusicPosition(p);
            set_time(p);
            emitPlayChange();
            draw();
        }
        else if ((c == KEY_RIGHT || c == KEY_SRIGHT) && music) {
            double d = (c == KEY_RIGHT ? 1 : 5);
            double p = Mix_GetMusicPosition(music) + d;
            p = min(double(track_len), p);
            Mix_SetMusicPosition(p);
            set_time(p);
            emitPlayChange();
            draw();
        }

        // 13) Toggle shuffle / repeat
        else if (c == 's') {
            settings.shuffle_default = !settings.shuffle_default;
            if (music && cur >= 0) build_pl(playlist[order[cur]]);
            emitPlayChange();
            draw();
        }
        else if (c == 'r') {
            settings.repeat_mode_default = (settings.repeat_mode_default + 1) % 3;
            emitPlayChange();
            draw();
        }

        // 14) Quit Ctrl-C
        else if (c == 3) {
            break;
        }

        // 15) Track finished callback
        if (done_cb) {
            done_cb = false;
            if (!Mix_PlayingMusic()) {
                nextFwd();
                emitPlayChange();
                draw();
            }
        }
    }


    // 7) Cleanup
    if(music) Mix_FreeMusic(music);
    Mix_CloseAudio();
    endwin();
    SDL_Quit();
    save_settings();
    return 0;
}
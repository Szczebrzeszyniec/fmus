#!/usr/bin/env bash
set -e
echo "---------------------------------------"
echo "          Build & Install fmus"
# echo "   (C++17, SDL2, SDL2_mixer, ncurses)"
echo "---------------------------------------"
echo
echo "Detecting package manager..."
if command -v apt >/dev/null; then
    pkg_mgr="apt"
    echo "[+] Detected apt"
elif command -v dnf >/dev/null; then
    pkg_mgr="dnf"
    echo "[+] Detected dnf"
elif command -v pacman >/dev/null; then
    pkg_mgr="pacman"
    echo "[+] Detected pacman"
elif command -v nix-env >/dev/null; then
    pkg_mgr="nix"
    echo "[!] Detected NixOS (nix-env)"
else
    echo "[!] No supported package manager found, install deps manually (listed on github)" >&2
    exit 1
fi
echo

install_with_apt() {
    declare -a pkgs=( \
      g++ \
      libsdl2-dev \
      libsdl2-mixer-dev \
      libncursesw5-dev \
    )
    sudo apt update -y
    for pkg in "${pkgs[@]}"; do
        echo "Checking if '$pkg' is installed..."
        if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
            echo "  • $pkg is already installed."
        else
            echo "  • Installing $pkg..."
            sudo apt install -y "$pkg"
        fi
    done
}

install_with_dnf() {
    declare -a pkgs=( \
      gcc-c++ \
      SDL2-devel \
      SDL2_mixer-devel \
      ncurses-devel \
    )
    for pkg in "${pkgs[@]}"; do
        echo "Checking if '$pkg' is installed..."
        if rpm -q "$pkg" >/dev/null 2>&1; then
            echo "  • $pkg already installed."
        else
            echo "  • Installing $pkg..."
            sudo dnf install -y "$pkg"
        fi
    done
}

install_with_pacman() {
    declare -a pkgs=( \
      gcc \
      sdl2 \
      sdl2_mixer \
      ncurses \
    )
    sudo pacman -Sy --noconfirm
    for pkg in "${pkgs[@]}"; do
        echo "Checking if '$pkg' is installed..."
        if pacman -Qi "$pkg" &>/dev/null; then
            echo "  • $pkg already installed."
        else
            echo "  • Installing $pkg..."
            sudo pacman -S --noconfirm "$pkg"
        fi
    done
}

install_with_nix() {
    echo "[!] NixOS detected."
    echo "    Please install dependencies manually:"
    echo "    nix-env -iA nixpkgs.gcc nixpkgs.SDL2 nixpkgs.SDL2_mixer nixpkgs.ncurses"
    exit 1
}

echo "Installing required dependencies..."
case "$pkg_mgr" in
    apt) install_with_apt ;;
    dnf) install_with_dnf ;;
    pacman) install_with_pacman ;;
    nix) install_with_nix ;;
esac

echo
echo "Building fmus..."
if g++ -std=c++17 -o fmus main.cpp \
     $(sdl2-config --cflags --libs) \
     -lSDL2_mixer -lncursesw -pthread; then
    echo "  Build succeeded."
else
    echo "  Build failed." >&2
    exit 1
fi

chmod +x fmus
echo "Installed 'fmus' executable."

echo "Copying to /usr/local/bin..."
sudo cp fmus /usr/bin/
echo "Done."

echo
echo "fmus has been installed successfully. Run it via:"
echo "  fmus"

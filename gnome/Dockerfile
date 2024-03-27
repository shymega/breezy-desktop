# To run the build from the package root:
#    docker build -f ./gnome/Dockerfile -t "breezy-gnome" .
#    docker run --rm -i -v ./gnome:/breezy-gnome "breezy-gnome"
#
# To run gnome-shell:
#    LD_LIBRARY_PATH=./gnome/out/lib ./gnome/out/bin/gnome-shell

# Use the base Arch Linux image
FROM ghcr.io/steamdeckhomebrew/holo-base:latest

RUN pacman -Syu --noconfirm
RUN pacman -Sydd --noconfirm gnome-shell mutter clutter cogl sassc libical gtk4 glib2 gnome-menus gnome-shell-extensions gsettings-desktop-schemas gobject-introspection gobject-introspection-runtime gcr-4 libdisplay-info gjs js102 startup-notification wayland xorg-xwayland xorg-xlsclients qt5-wayland glfw-wayland

ENTRYPOINT [ "/breezy-gnome/copy_libraries.sh" ]
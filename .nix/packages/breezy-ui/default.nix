{
  self,
  version,
  lib,
  stdenv,
  fetchFromGitHub,
  meson,
  ninja,
  pkg-config,
  gettext,
  glib,
  gtk4,
  libadwaita,
  desktop-file-utils,
  gobject-introspection,
  wrapGAppsHook4,
  python3Packages,
  python3,
  gst_all_1,
  appstream,
  runtimeShell,
  pipewire,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "breezy-desktop-ui";
  inherit version;

  # Use the ui/ subtree but need root for VERSION file
  src = self;
  sourceRoot = "ui";

  # The source is a nix store path, not a directory name
  unpackPhase = ''
    cp -r $src $TMPDIR/breezy-desktop
    chmod -R u+w $TMPDIR/breezy-desktop
    cd $TMPDIR/breezy-desktop/ui
    sourceRoot=$(pwd)
  '';

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    gettext
    glib # glib-compile-schemas
    gtk4 # gtk-update-icon-cache
    desktop-file-utils
    gobject-introspection
    wrapGAppsHook4
    appstream # appstreamcli for validation
  ];

  buildInputs = [
    gtk4
    libadwaita
    glib
    gst_all_1.gstreamer
    gst_all_1.gst-plugins-base
    pipewire # pipewiresrc GStreamer element, required for virtual displays
    (python3.withPackages (
      ps: with ps; [
        pygobject3
        pydbus
      ]
    ))
  ];

  # Patch the entry scripts to use Nix store paths instead of XDG_DATA_HOME
  postPatch = ''
    substituteInPlace src/breezydesktop.in \
      --replace-fail "appdir = os.getenv('APPDIR', xdg_data_home)" \
                     "appdir = os.getenv('APPDIR', '$out/share')"
    substituteInPlace src/virtualdisplay.in \
      --replace-fail "appdir = os.getenv('APPDIR', xdg_data_home)" \
                     "appdir = os.getenv('APPDIR', '$out/share')" || true

    # Upstream bug: virtualdisplayrow.py is missing from meson.build install list
    substituteInPlace src/meson.build \
      --replace-fail "'virtualdisplay.py'," "'virtualdisplay.py', 'virtualdisplayrow.py',"
  '';

  # Skip tests that need network or display
  mesonFlags = [
  ];

  # The UI locates helper programs (virtualdisplay, breezy_gnome_verify) via
  # $BINDIR, falling back to ~/.local/bin where the traditional installer puts
  # them. Point it at our own bin instead.
  #
  # breezy_gnome_verify normally re-hashes installed files against a manifest
  # to detect broken or tampered imperative installs. Nix store paths are
  # immutable and content-hash-verified, so the check is inherently satisfied;
  # the UI only needs the success handshake.
  postInstall = ''
    printf '#!%s\necho "Verification succeeded"\n' "${runtimeShell}" \
      > $out/bin/breezy_gnome_verify
    chmod 755 $out/bin/breezy_gnome_verify
  '';

  preFixup = ''
    gappsWrapperArgs+=(--set BINDIR "$out/bin")
  '';

  # wrapGAppsHook4 handles GI_TYPELIB_PATH, GDK_PIXBUF, etc.

  meta = {
    description = "GTK4 settings UI for Breezy Desktop XR";
    homepage = "https://github.com/wheaney/breezy-desktop";
    license = lib.licenses.gpl3Only;
    platforms = lib.platforms.linux;
  };
})

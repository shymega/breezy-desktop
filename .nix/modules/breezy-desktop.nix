{ self }:
{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.services.breezy-desktop;
  package = self.packages.${pkgs.stdenv.hostPlatform.system}.breezy-desktop;
in
{
  options.services.breezy-desktop = {
    enable = lib.mkEnableOption "Breezy Desktop XR virtual display";
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ package ];
    systemd.packages = [ package ];
    services.udev.packages = [ package ];
    boot.kernelModules = [ "uinput" ];
  };
}

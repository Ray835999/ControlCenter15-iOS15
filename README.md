# ControlCenter15

A from-scratch iOS 15 control center customization tweak for iPhone 6s / iOS 15.8.8 / arm64 / rootless (palera1n + ElleKit).

Unlike ControlCenter18 (which redraws the whole CC and only targets iOS 18), this tweak hooks only classes that actually exist on iOS 15:
- CCUIContentModuleContainerViewController (rounds each module tile)
- CCUIRoundButton (enlarges the round toggles)
- CCUIModuleCollectionViewController

No version gate, no fake "other tweaks incompatible" banner.

Optional preferences (edit with Filza): /var/jb/Library/Preferences/com.ray.cc15.plist
  ModuleCornerRadius (double, default 22)
  ButtonCornerRadius (double, default 34)
  ModuleBgRed/Green/Blue/Alpha (0..1, optional module tint)

Download the latest .deb from Releases.

# airgraph.spec — see which Wi-Fi channels are crowded, and how strong each
# access point is.
#
# airgraph (https://github.com/wegweg7979/airgraph, MIT) is a plain C++17
# qmake6/Qt6 app: `bin/build` is the build, `pkgbuild/` the launcher entry and
# icon — the same shape as hype.spec in the omedora COPR.
#
# COPR-safe: every dependency is a BuildRequires and the build needs no network.

# qmake's release config builds an optimized binary without -g, so there is no
# debug info to extract; an empty debuginfo subpackage would fail the build.
%global debug_package %{nil}

Name:           airgraph
Version:        0.1.0
Release:        1%{?dist}
Summary:        See which Wi-Fi channels are crowded, and how strong each access point is

License:        MIT
URL:            https://github.com/wegweg7979/airgraph

# GitHub's tag archive unpacks to airgraph-<version>/. bin/spec-sources fetches
# it and rewrites airgraph.spec.sources with its sha256 — after the tag exists,
# which is why the hash is a separate commit from the version bump.
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

# Compiled for x86_64 (the only arch the hurunagus images target right now).
ExclusiveArch:  x86_64

BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  desktop-file-utils
# airgraph.pro is `QT += core gui qml quick quickcontrols2 dbus`, and the test
# build adds testlib. Qt Quick Controls — including the Material style the
# window sets with QQuickStyle — ships inside qt6-qtdeclarative on Fedora 45;
# there is no qt6-qtquickcontrols2 package any more. QtDBus and QtTest come with
# qt6-qtbase-devel.
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel

# Run, not linked: NetworkManager is read over the system bus (which is what
# keeps the scan unprivileged — no root, no polkit prompt), and the Wayland
# platform plugin is not optional on a Hyprland-only image.
Requires:       NetworkManager
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtwayland
# iw only feeds the band plan (`iw phy <phy> info`, which needs no privilege);
# without it the app falls back to the IEEE channel table.
Recommends:     iw

%description
airgraph shows what is actually on the Wi-Fi air: which channels are crowded, by
whom, and how strong. It scans with NetworkManager over D-Bus and draws a channel
graph with a per-channel congestion strip, a signal-over-time panel and the raw
access point table behind them.

It is read-only by design: it never connects, disconnects, or changes radio or
regulatory state. The same data is available without a window for scripts and
agents (`airgraph check`, `airgraph scan --json`, `airgraph export --csv`).

%prep
%autosetup -n %{name}-%{version}

%build
./bin/build

%install
install -D -m 0755 build/airgraph %{buildroot}%{_bindir}/airgraph
install -D -m 0644 pkgbuild/airgraph.svg \
  %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/airgraph.svg
install -D -m 0644 pkgbuild/airgraph.desktop \
  %{buildroot}%{_datadir}/applications/airgraph.desktop

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/airgraph.desktop
# Offscreen, no display, no D-Bus and no scanning — the suite injects a fake
# scanner — so it runs in a build chroot, and a release that fails its own tests
# does not publish.
QT_QPA_PLATFORM=offscreen ./bin/test

%files
%license LICENSE
%doc README.md
%{_bindir}/airgraph
%{_datadir}/icons/hicolor/scalable/apps/airgraph.svg
%{_datadir}/applications/airgraph.desktop

%changelog
* Fri Nov 13 2026 wegweg7979 <diode-crumble-0h@icloud.com> - 0.1.0-1
- First release: the channel graph, the live access point table, signal over
  time, CSV/JSON export, and the headless commands (check / scan / export).

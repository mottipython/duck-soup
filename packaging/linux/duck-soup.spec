Name:           duck-soup
Version:        %{version}
Release:        1%{?dist}
Summary:        Duck Soup, a cartoon screensaver of the Marx Brothers' sight gags
License:        MIT
Source0:        duck-soup-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
Duck Soup, a cartoon screensaver of the sight gags from the Marx Brothers'
Duck Soup (1933), in the order of the film: the new leader of Freedonia arriving
down a fireman's pole, the lemonade stand and its hat shuffle, the sidecar that
never goes anywhere, the radio that will not stop playing, the broken mirror,
Paul Revere's ride, and a war fought in a different uniform every time. With
public-domain band recordings from before 1926.

On Wayland desktops it starts by itself after five minutes idle.
duck-soup-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/duck-soup

%files
%license LICENSE
%doc README.md
%{_bindir}/duck-soup
%{_bindir}/duck-soup-screensaver
%{_datadir}/applications/duck-soup.desktop
%dir %{_datadir}/screensaver-service
%dir %{_datadir}/screensaver-service/savers
%{_datadir}/screensaver-service/savers/duck-soup.desktop
%{_datadir}/icons/hicolor/256x256/apps/duck-soup.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/duck-soup-screensaver.desktop

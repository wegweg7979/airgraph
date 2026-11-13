# Airgraph

See which Wi-Fi channels are crowded, and how strong each access point is: a
channel graph in the shape of the Wi-Fi Analyzer app, plus the raw table behind
it and the signal over time.

![airgraph showing the 2.4 GHz channel graph, the congestion strip, the signal-over-time panel and the access point table](assets/screenshot.png)

Scans with NetworkManager over D-Bus — no root, no polkit prompt — and draws
what is on the air. It never connects, disconnects, or changes radio or
regulatory state: it reads the air and reports it.




## Fedora package (COPR)

```sh
sudo dnf copr enable wegweg7979/airgraph
sudo dnf install airgraph
```



## Licence

MIT. The window shape, theme follower and translator follow Omarchy's own apps
(Omacut/Monologue, MIT © David Heinemeier Hansson) — see `LICENSE`.

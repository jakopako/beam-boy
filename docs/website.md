# Website hosting

All published content lives in [website](../website): the landing page is
[index.html](../website/index.html), with its styles in
[style.css](../website/style.css). It is plain HTML/CSS: no dependencies, build
step, external fonts, or JavaScript. The [games](../website/games) directory is
published alongside it for the device's store.

Implementation notes and this hosting guide remain in `docs`, outside the
published site. They are still public in the repository, but are not included
in the Pages artifact.

## GitHub Pages

In **Settings > Pages > Build and deployment > Source**, switch from
**Deploy from a branch** to **GitHub Actions**. This is required: branch-based
publishing only supports the repository root or `/docs`.

The [Pages workflow](../.github/workflows/pages.yml) runs on every push to
`main`, checks that the game index is up to date, and uploads **only `website`**.
It can also be run manually from the Actions tab on `main`. Other branches
cannot publish the production site. Deployment uses the `github-pages`
environment; any configured protection rules must allow `main`.

There is no Jekyll build: static files, including cartridge bytes, are uploaded
unchanged. A `CNAME` file is not needed (and is ignored by custom Actions
deployments); the custom domain is configured in Pages settings instead.

## Connect beamboy.ch

1. Before pointing DNS at GitHub, [verify ownership of the domain in your GitHub
   account](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/verifying-your-custom-domain-for-github-pages).
   Add the TXT record GitHub provides and keep it after verification. This
   protects the domain from being claimed by another GitHub user.
2. Select **GitHub Actions** as the Pages source, then merge the website changes
   into `main`. In the repository's
   [Pages settings](https://github.com/jakopako/beam-boy/settings/pages), set
   **Custom domain** to `beamboy.ch` and save it.
   Configure this on GitHub **before** adding the DNS records below.
3. At the domain's DNS provider, set these records. `@` means the apex
   (`beamboy.ch`); some providers use a blank name or the full domain instead.

   | Type | Name | Value |
   | ---- | ---- | ----- |
   | A | @ | 185.199.108.153 |
   | A | @ | 185.199.109.153 |
   | A | @ | 185.199.110.153 |
   | A | @ | 185.199.111.153 |
   | CNAME | www | jakopako.github.io |

   The `www` record is optional; GitHub redirects it to the apex when both
   are configured. Its target is `jakopako.github.io`, **not** a URL with
   `https://` or `/beam-boy`.

   Remove conflicting web-hosting A/AAAA/ALIAS records at the apex and any
   conflicting `www` records. Do not remove unrelated mail/MX/TXT records.
   Do not add wildcard records. If enabling IPv6, add all four of GitHub's
   [documented AAAA records](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site)
   alongside the A records, rather than leaving an old host's AAAA record.
4. Allow up to 24 hours for DNS propagation and certificate provisioning.
   Once GitHub's DNS check passes and the certificate is ready, enable
   **Enforce HTTPS** in Pages settings.
5. Check `https://beamboy.ch/`, `https://beamboy.ch/games/index.json`, and a
   cartridge URL from the index. Flash the updated firmware onto the test
   device and confirm a store download succeeds.

Changing the custom domain redirects the old
`https://jakopako.github.io/beam-boy/` URLs to `https://beamboy.ch/`. The store's
default index URL and the index generator now use the new domain directly:
the device's HTTP client does not follow redirects. Firmware built before this
change needs reflashing. No transitional compatibility layer is included.

GitHub Pages hosting and its HTTPS certificate are free for this public
repository; the domain's registration/renewal is still paid at your registrar.
This only hosts static content, not a backend.

## Local preview and updates

From the repository root:

```powershell
python -m http.server 8000 --bind 127.0.0.1 --directory website
```

Open `http://127.0.0.1:8000/`. Stop the server with Ctrl+C.
Edit the HTML/CSS, then merge changes into `main` to publish them.
Relative stylesheet paths work both on the custom domain and under the old
GitHub Pages project path.

When publishing cartridges, continue to regenerate and check the index:

```powershell
python tools\build_store_index.py
python tools\build_store_index.py --check
```

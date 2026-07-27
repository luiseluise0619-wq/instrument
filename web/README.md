# Slyce — product site

One self-contained page. Every image, style and script is inlined, so the
deploy makes no outbound requests at all: nothing to break when a CDN is slow
and no font that can silently fail to load.

## Deploying

Static — there is no build step and no framework.

On Vercel, import this repository and set **Root Directory** to `web`.
Framework Preset is **Other**; leave Build Command and Output Directory empty.

`vercel.json` only adds three response headers (`X-Content-Type-Options`,
`Referrer-Policy`, `X-Frame-Options`). The site works without it.

## Editing

`index.html` is generated: the page body is authored separately and wrapped in
a document head. Editing the file directly is fine for small changes, but the
base64 image blobs make it awkward to read - search for `<!-- ` section
comments rather than scrolling.

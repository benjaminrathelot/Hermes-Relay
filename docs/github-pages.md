# GitHub Pages Setup

## Current Recommendation

According to the current GitHub Pages documentation, a repository can publish a Pages site from:

- a branch and folder
- or a custom GitHub Actions workflow

If publishing from a branch, GitHub allows:

- the root of the source branch
- or a `/docs` folder on the source branch

The source branch can be any branch. The long-standing `gh-pages` branch is still a practical convention for static output.

Official GitHub documentation:

- <https://docs.github.com/en/pages/getting-started-with-github-pages/configuring-a-publishing-source-for-your-github-pages-site>
- <https://docs.github.com/pages/quickstart>

## Hermes Relay Site Layout

For this repository:

- `site/` on `main` contains the maintainable source for the landing page
- `gh-pages` is intended to contain the published static files at branch root
- `.nojekyll` is included so GitHub Pages serves the files directly without trying to run Jekyll processing

## Publishing Model

Recommended setup:

1. keep development of the site source on `main` under `site/`
2. publish the rendered static files to `gh-pages`
3. configure GitHub Pages to deploy from branch `gh-pages`, folder `/`

This keeps the main codebase clean while preserving a simple static deployment path.

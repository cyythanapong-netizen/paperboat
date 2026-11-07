# Sphinx configuration for the Paperboat documentation (docs/api).
# Build: npm run docs   (or ./scripts/build_docs.sh)
#
# The theme is Furo, styled to match the Paperboat design system (apps/web/DESIGN.md):
# flat mono, ink on grey panels with hairlines, recessed grey fields for code,
# and one signal colour for live/active accents.
# Tokens are synced from apps/web/src/tokens.css via docs/gen_tokens.py into docs/_static/tokens.css.

project = "Paperboat"
author = "Paperboat"
release = "0"
version = "0 (protocol paperboat/0)"

extensions = ["sphinx.ext.githubpages"]
exclude_patterns = ["_build"]

html_theme = "furo"
templates_path = ["_templates"]
html_static_path = ["../_static"]
html_css_files = ["tokens.css", "docs.css"]
html_title = "Paperboat"
html_baseurl = "https://cyythanapong-netizen.github.io/paperboat/"
html_favicon = "../_static/favicon.svg"
html_show_sphinx = False
highlight_language = "none"
primary_domain = None
cpp_index_common_prefix = ["paperboat::"]

html_theme_options = {
    "source_repository": "https://github.com/cyythanapong-netizen/paperboat/",
    "source_branch": "dev",
    "source_directory": "docs/api/",
    "sidebar_hide_name": True,
    "navigation_with_keys": True,
    "light_css_variables": {
        "font-stack": '"Geist Sans", system-ui, -apple-system, sans-serif',
        "font-stack--headings": '"Geist Sans", system-ui, -apple-system, sans-serif',
        "font-stack--monospace": '"Geist Mono", ui-monospace, monospace',
        "color-brand-primary": "#ff5a1f",
        "color-brand-content": "#ff5a1f",
        "color-background-primary": "#eaeae6",
        "color-background-secondary": "#dcdcd8",
        "color-background-hover": "#f2f2ef",
        "color-background-border": "#cfcfca",
        "color-foreground-primary": "#161616",
        "color-foreground-secondary": "#55554f",
        "color-foreground-muted": "#55554f",
        "color-foreground-border": "#9d9d97",
        "color-sidebar-background": "#e1e1dd",
        "color-sidebar-background-border": "#cfcfca",
        "color-sidebar-item-background--current": "#eaeae6",
        "color-sidebar-item-background--hover": "#e6e6e2",
        "color-sidebar-link-text": "#55554f",
        "color-sidebar-link-text--top-level": "#161616",
        "color-sidebar-brand-text": "#161616",
        "color-sidebar-search-background": "#dcdcd7",
        "color-sidebar-search-border": "#cfcfca",
        "color-code-background": "#dcdcd7",
        "color-code-foreground": "#161616",
        "color-inline-code-background": "#dcdcd7",
        "color-table-border": "#cfcfca",
        "color-table-header-background": "#eaeae6",
        "color-api-background": "#f2f2ef",
        "color-api-background-hover": "#e6e6e2",
        "color-api-overall": "#161616",
        "color-api-name": "#161616",
        "color-api-pre-name": "#55554f",
        "color-api-paren": "#55554f",
        "color-api-keyword": "#55554f",
        "color-admonition-background": "#f2f2ef",
        "color-admonition-title-background": "#f2f2ef",
        "color-admonition-title": "#161616",
        "color-link": "#161616",
        "color-link--hover": "#161616",
        "color-link-underline": "#9d9d97",
        "color-link-underline--hover": "#161616",
    },
    "dark_css_variables": {
        "font-stack": '"Geist Sans", system-ui, -apple-system, sans-serif',
        "font-stack--headings": '"Geist Sans", system-ui, -apple-system, sans-serif',
        "font-stack--monospace": '"Geist Mono", ui-monospace, monospace',
        "color-brand-primary": "#ff6a30",
        "color-brand-content": "#ff6a30",
        "color-background-primary": "#1a1a1a",
        "color-background-secondary": "#111111",
        "color-background-hover": "#222222",
        "color-background-border": "#2e2e2e",
        "color-foreground-primary": "#e9e9e4",
        "color-foreground-secondary": "#a3a39d",
        "color-foreground-muted": "#a3a39d",
        "color-foreground-border": "#4a4a4a",
        "color-sidebar-background": "#151515",
        "color-sidebar-background-border": "#2e2e2e",
        "color-sidebar-item-background--current": "#1a1a1a",
        "color-sidebar-item-background--hover": "#1f1f1f",
        "color-sidebar-link-text": "#a3a39d",
        "color-sidebar-link-text--top-level": "#e9e9e4",
        "color-sidebar-brand-text": "#e9e9e4",
        "color-sidebar-search-background": "#111111",
        "color-sidebar-search-border": "#2e2e2e",
        "color-code-background": "#111111",
        "color-code-foreground": "#e9e9e4",
        "color-inline-code-background": "#111111",
        "color-table-border": "#2e2e2e",
        "color-table-header-background": "#1a1a1a",
        "color-api-background": "#222222",
        "color-api-background-hover": "#2a2a2a",
        "color-api-overall": "#e9e9e4",
        "color-api-name": "#e9e9e4",
        "color-api-pre-name": "#a3a39d",
        "color-api-paren": "#a3a39d",
        "color-api-keyword": "#a3a39d",
        "color-admonition-background": "#222222",
        "color-admonition-title-background": "#222222",
        "color-admonition-title": "#e9e9e4",
        "color-link": "#e9e9e4",
        "color-link--hover": "#e9e9e4",
        "color-link-underline": "#4a4a4a",
        "color-link-underline--hover": "#e9e9e4",
    },
}

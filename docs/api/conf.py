# Sphinx configuration for the Paperboat documentation (docs/api).
# Build: npm run docs   (or ./scripts/build_docs.sh)
#
# The theme is Shibuya, styled with Paperboat fonts and orange accent.

project = "Paperboat"
author = "Paperboat"
release = "0"
version = "0 (protocol paperboat/0)"

extensions = ["sphinx.ext.githubpages"]
exclude_patterns = ["_build"]

html_theme = "shibuya"
html_static_path = ["../_static"]
html_css_files = ["fonts.css"]
html_title = "Paperboat"
html_baseurl = "https://cyythanapong-netizen.github.io/paperboat/"
html_favicon = "../_static/favicon.svg"
html_show_sphinx = False
highlight_language = "none"
primary_domain = None
cpp_index_common_prefix = ["paperboat::"]

html_theme_options = {
    "accent_color": "orange",
    "color_mode": "auto",
    "light_logo": "_static/logo-light.svg",
    "dark_logo": "_static/logo-dark.svg",
    "github_url": "https://github.com/cyythanapong-netizen/paperboat",
    "nav_links": [
        {
            "title": "Manuals",
            "children": [
                {"title": "User Manual", "url": "user_manual"},
                {"title": "Technical Manual", "url": "technical_manual"},
                {"title": "Tutorials", "url": "tutorials"},
                {"title": "Quickstart", "url": "quickstart"},
            ],
        },
        {
            "title": "API Reference",
            "children": [
                {"title": "C++ SDK", "url": "cpp"},
                {"title": "Python SDK", "url": "python"},
                {"title": "Toolkit", "url": "toolkit"},
                {"title": "Protocol", "url": "protocol"},
                {"title": "Descriptors", "url": "descriptor"},
            ],
        },
    ],
}

# Sphinx configuration for the Paperboat API reference (docs/api).
# Build: npm run docs   (sphinx-build -W -b html docs/api docs/api/_build)

project = "Paperboat"
author = "Paperboat"
release = "0"
version = "0 (protocol paperboat/0)"

extensions = []
exclude_patterns = ["_build"]
html_theme = "alabaster"
html_theme_options = {"description": "A framework for apps built from a station, a flow and a dashboard."}
html_static_path = ["_static"]
highlight_language = "none"
primary_domain = None
cpp_index_common_prefix = ["paperboat::"]

html_css_files = ["paperboat.css"]
html_js_files = ["theme.js"]
